#include "Saturator.h"

#include <cmath>

namespace aurora
{
namespace
{
constexpr int probeSize = 32;

[[nodiscard]] inline float biasScaleFor(int model) noexcept
{
    switch (model)
    {
        case 1:  return 0.60f; // Tape
        case 2:  return 1.20f; // Diode
        case 3:  return 0.80f; // Transistor
        case 0:
        default: return 1.00f; // Tube
    }
}

[[nodiscard]] inline float tubeCurve(float v) noexcept
{
    return std::tanh(v * (1.0f + 0.28f * std::abs(v)));
}

[[nodiscard]] inline float tapeCurve(float v) noexcept
{
    return v / (1.0f + 0.42f * std::abs(v));
}

[[nodiscard]] inline float diodeCurve(float v) noexcept
{
    constexpr float a = 1.7f;
    return std::copysign(1.0f - std::exp(-a * std::abs(v)), v);
}

[[nodiscard]] inline float transistorCurve(float v) noexcept
{
    const float t = std::tanh(1.9f * v);
    const float s = v / std::sqrt(1.0f + v * v);
    return 0.62f * t + 0.38f * s;
}

[[nodiscard]] inline float shape(int model, float x, float bias) noexcept
{
    const float b = bias * biasScaleFor(model);

    switch (model)
    {
        case 1:  return tapeCurve(x + b) - tapeCurve(b);
        case 2:  return diodeCurve(x + b) - diodeCurve(b);
        case 3:  return transistorCurve(x + b) - transistorCurve(b);
        case 0:
        default: return tubeCurve(x + b) - tubeCurve(b);
    }
}

// Memoryless RMS gain of the current curve, estimated with a fixed sine probe.
// Returns the multiplier that restores the probe RMS before the shaper.
[[nodiscard]] float estimateAutoGain(int model, float driveGain, float bias,
                                     const std::array<float, probeSize>& probe) noexcept
{
    double inEnergy = 0.0;
    double outEnergy = 0.0;

    for (int i = 0; i < probeSize; ++i)
    {
        const double x = static_cast<double>(probe[static_cast<size_t>(i)]) * driveGain;
        const double y = shape(model, static_cast<float>(x), bias);
        inEnergy += x * x;
        outEnergy += y * y;
    }

    if (outEnergy < 1.0e-12)
        return 1.0f;

    const double g = std::sqrt(inEnergy / outEnergy);
    return juce::jlimit(0.05f, 20.0f, static_cast<float>(g));
}
} // namespace

SaturatorModule::SaturatorModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id),
      oversampling(static_cast<size_t>(kMaxChannels), 3,
                   juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                   true, true)
{
    const auto prefix = instancePrefix(id);
    model.bind(state, prefix + ".model");
    drive.bind(state, prefix + ".drive");
    tone.bind(state, prefix + ".tone");
    bias.bind(state, prefix + ".bias");
    autoGain.bind(state, prefix + ".autoGain");
    output.bind(state, prefix + ".output");
    mix.bind(state, prefix + ".mix");
}

void SaturatorModule::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, 2, numChannels);
    maxBlock = juce::jmax(1, maxBlockSize);

    workBuffer.setSize(2, maxBlock, false, true, false);
    workBuffer.clear();

    oversampling.initProcessing(static_cast<size_t>(maxBlock));
    oversampling.reset();

    latency = juce::jmax(0, static_cast<int>(std::lround(oversampling.getLatencyInSamples())));
    dryLine.prepare(latency + 8, 2, maxBlock);

    drive.prepare(sampleRate, 20.0, 6.0f);
    tone.prepare(sampleRate, 20.0, 0.0f);
    bias.prepare(sampleRate, 20.0, 0.1f);
    output.prepare(sampleRate, 20.0, 0.0f);
    mix.prepare(sampleRate, 20.0, 100.0f);

    for (auto& dc : dcBlockers)
        dc.prepare(sampleRate * static_cast<double>(oversamplingFactor));

    toneCoeff = std::exp(-dsp::twoPi * 1000.0 / sampleRate);

    for (int i = 0; i < probeSize; ++i)
        probe[static_cast<size_t>(i)] = 0.5f * static_cast<float>(
            std::sin(dsp::twoPi * static_cast<double>(i) / static_cast<double>(probeSize)));

    reset();
}

void SaturatorModule::reset() noexcept
{
    drive.snap(drive.target());
    tone.snap(tone.target());
    bias.snap(bias.target());
    output.snap(output.target());
    mix.snap(mix.target());

    for (auto& dc : dcBlockers)
        dc.reset();

    dryLine.reset();
    oversampling.reset();

    toneState.fill(0.0);
    level.store(-120.0f, std::memory_order_relaxed);
}

void SaturatorModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0 || maxBlock <= 0)
        return;

    const int count = juce::jmin(n, workBuffer.getNumSamples());
    if (count <= 0)
        return;

    drive.updateTarget();
    tone.updateTarget();
    bias.updateTarget();
    output.updateTarget();
    mix.updateTarget();

    const int modelIndex = juce::jlimit(0, numModels - 1, model.getChoice());
    const bool autoOn = autoGain.getBool();

    for (int ch = 0; ch < 2; ++ch)
    {
        if (ch < numCh)
            workBuffer.copyFrom(ch, 0, buffer, ch, 0, count);
        else
            workBuffer.clear(ch, 0, count);
    }

    juce::dsp::AudioBlock<float> baseBlock(workBuffer.getArrayOfWritePointers(), 2, 0,
                                           static_cast<size_t>(count));
    auto upBlock = oversampling.processSamplesUp(baseBlock);
    float* upL = upBlock.getChannelPointer(0);
    float* upR = upBlock.getChannelPointer(1);

    constexpr int subBlock = 32;

    for (int offset = 0; offset < count; offset += subBlock)
    {
        const int c = juce::jmin(subBlock, count - offset);
        const float driveDb = drive.smoother.skip(c);
        const float biasValue = bias.smoother.skip(c);
        const float driveGain = dsp::dbToGain(driveDb);
        const float autoGainValue = autoOn ? estimateAutoGain(modelIndex, driveGain, biasValue, probe)
                                           : 1.0f;

        const int start = offset * oversamplingFactor;
        const int end = start + c * oversamplingFactor;

        for (int i = start; i < end; ++i)
        {
            float l = shape(modelIndex, upL[i] * driveGain, biasValue);
            float r = shape(modelIndex, upR[i] * driveGain, biasValue);

            l = static_cast<float>(dcBlockers[0].process(static_cast<double>(l)));
            r = static_cast<float>(dcBlockers[1].process(static_cast<double>(r)));

            upL[i] = dsp::sanitize(l * autoGainValue);
            upR[i] = dsp::sanitize(r * autoGainValue);
        }
    }

    oversampling.processSamplesDown(baseBlock);

    float peak = 0.0f;

    for (int offset = 0; offset < count; offset += subBlock)
    {
        const int c = juce::jmin(subBlock, count - offset);
        const float toneDb = tone.smoother.skip(c);
        const float outputDb = output.smoother.skip(c);
        const float mixPct = juce::jlimit(0.0f, 1.0f, mix.smoother.skip(c) * 0.01f);
        const float dryGain = 1.0f - mixPct;
        const float wetGain = mixPct * dsp::dbToGain(outputDb);
        const float lowGain = dsp::dbToGain(-0.5f * toneDb);
        const float highGain = dsp::dbToGain(0.5f * toneDb);

        for (int i = offset; i < offset + c; ++i)
        {
            const float inL = buffer.getSample(0, i);
            const float inR = numCh > 1 ? buffer.getSample(1, i) : inL;

            dryLine.push(0, inL);
            dryLine.push(1, numCh > 1 ? inR : 0.0f);
            const float dryL = dryLine.read(0, latency);
            const float dryR = dryLine.read(1, latency);
            dryLine.advance();

            const float wetL = workBuffer.getSample(0, i);
            const float wetR = workBuffer.getSample(1, i);

            const double lowL = (1.0 - toneCoeff) * static_cast<double>(wetL)
                              + toneCoeff * toneState[0];
            const double lowR = (1.0 - toneCoeff) * static_cast<double>(wetR)
                              + toneCoeff * toneState[1];
            toneState[0] = std::isfinite(lowL) ? lowL : 0.0;
            toneState[1] = std::isfinite(lowR) ? lowR : 0.0;

            const float tiltL = static_cast<float>(lowL) * lowGain
                              + (wetL - static_cast<float>(lowL)) * highGain;
            const float tiltR = static_cast<float>(lowR) * lowGain
                              + (wetR - static_cast<float>(lowR)) * highGain;

            const float outL = dsp::sanitize(dryL * dryGain + tiltL * wetGain);
            const float outR = dsp::sanitize(dryR * dryGain + tiltR * wetGain);

            buffer.setSample(0, i, outL);
            if (numCh > 1)
                buffer.setSample(1, i, outR);

            peak = juce::jmax(peak, std::abs(outL), numCh > 1 ? std::abs(outR) : 0.0f);
        }
    }

    level.store(dsp::gainToDb(peak), std::memory_order_relaxed);
}
} // namespace aurora
