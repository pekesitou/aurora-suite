#include "SpatialFX.h"

#include <cmath>

namespace aurora
{
namespace
{
constexpr int subBlock = 32;

[[nodiscard]] inline float mixLinear(float mix01) noexcept
{
    return juce::jlimit(0.0f, 1.0f, mix01);
}
} // namespace

//==============================================================================
StereoImagerModule::StereoImagerModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    width.bind(state, prefix + ".width");
    midGain.bind(state, prefix + ".mid");
    sideGain.bind(state, prefix + ".side");
    bassMonoFreq.bind(state, prefix + ".bassMonoFreq");
    bassMonoAmount.bind(state, prefix + ".bassMonoAmount");
}

void StereoImagerModule::prepare(double newSampleRate, int, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, 2, numChannels);

    width.prepare(sampleRate, 20.0, 100.0f);
    midGain.prepare(sampleRate, 20.0, 0.0f);
    sideGain.prepare(sampleRate, 20.0, 0.0f);
    bassMonoFreq.prepare(sampleRate, 20.0, 120.0f);
    bassMonoAmount.prepare(sampleRate, 20.0, 100.0f);

    corrCoeff = std::exp(-1.0 / juce::jmax(1.0, sampleRate * 0.3));
    reset();
}

void StereoImagerModule::reset() noexcept
{
    width.snap(width.target());
    midGain.snap(midGain.target());
    sideGain.snap(sideGain.target());
    bassMonoFreq.snap(bassMonoFreq.target());
    bassMonoAmount.snap(bassMonoAmount.target());

    sideLowPass.reset();
    sumLR = sumLL = sumRR = 0.0;
    correlationValue.store(0.0f, std::memory_order_relaxed);
}

void StereoImagerModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0)
        return;

    width.updateTarget();
    midGain.updateTarget();
    sideGain.updateTarget();
    bassMonoFreq.updateTarget();
    bassMonoAmount.updateTarget();

    auto* left = buffer.getWritePointer(0);
    auto* right = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

    float lastCorr = correlationValue.load(std::memory_order_relaxed);

    for (int offset = 0; offset < n; offset += subBlock)
    {
        const int c = juce::jmin(subBlock, n - offset);

        const float widthLin = juce::jlimit(0.0f, 2.0f, width.smoother.skip(c) * 0.01f);
        const float midLin = dsp::dbToGain(midGain.smoother.skip(c));
        const float sideLin = dsp::dbToGain(sideGain.smoother.skip(c));
        const float monoAmount = juce::jlimit(0.0f, 1.0f,
                                              bassMonoAmount.smoother.skip(c) * 0.01f);
        const double freq = juce::jlimit(20.0, sampleRate * 0.49,
                                         static_cast<double>(bassMonoFreq.smoother.skip(c)));
        sideLowPass.set(dsp::BiquadCoeffs::lowPass(sampleRate, freq, 0.707));

        for (int i = offset; i < offset + c; ++i)
        {
            const float inL = left[i];
            const float inR = right != nullptr ? right[i] : inL;

            float m = 0.5f * (inL + inR) * midLin;
            float s = 0.5f * (inL - inR) * sideLin * widthLin;

            const double lowSide = sideLowPass.process(static_cast<double>(s));
            s -= static_cast<float>(lowSide) * monoAmount;

            const float outL = dsp::sanitize(m + s);
            const float outR = dsp::sanitize(m - s);

            left[i] = outL;
            if (right != nullptr)
                right[i] = outR;

            const double corrL = static_cast<double>(outL);
            const double corrR = static_cast<double>(outR);
            sumLR = corrCoeff * sumLR + (1.0 - corrCoeff) * corrL * corrR;
            sumLL = corrCoeff * sumLL + (1.0 - corrCoeff) * corrL * corrL;
            sumRR = corrCoeff * sumRR + (1.0 - corrCoeff) * corrR * corrR;

            const double denom = std::sqrt(juce::jmax(1.0e-12, sumLL * sumRR));
            lastCorr = juce::jlimit(-1.0f, 1.0f, static_cast<float>(sumLR / denom));
        }
    }

    correlationValue.store(lastCorr, std::memory_order_relaxed);
}

//==============================================================================
ReverbModule::ReverbModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    size.bind(state, prefix + ".size");
    decay.bind(state, prefix + ".decay");
    predelay.bind(state, prefix + ".predelay");
    damping.bind(state, prefix + ".damping");
    diffusion.bind(state, prefix + ".diffusion");
    width.bind(state, prefix + ".width");
    lowCut.bind(state, prefix + ".lowcut");
    highCut.bind(state, prefix + ".highcut");
    mix.bind(state, prefix + ".mix");
    freeze.bind(state, prefix + ".freeze");
}

void ReverbModule::prepare(double newSampleRate, int, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, 2, numChannels);

    static constexpr double baseMs[numLines] = {
        29.71, 37.13, 41.09, 43.87, 47.53, 53.71, 59.33, 67.13
    };
    static constexpr double diffuserMs[numDiffusers] = { 4.77, 3.59, 12.73, 9.31 };

    for (int i = 0; i < numLines; ++i)
    {
        baseDelaySamples[static_cast<size_t>(i)] =
            static_cast<float>(baseMs[i] * 0.001 * sampleRate);
        const int maxSamples = static_cast<int>(std::ceil(
            baseDelaySamples[static_cast<size_t>(i)] * 1.62f)) + 8;
        lines[static_cast<size_t>(i)].prepare(maxSamples);
    }

    for (int i = 0; i < numDiffusers; ++i)
    {
        diffuserDelaySamples[static_cast<size_t>(i)] = static_cast<int>(
            std::lround(diffuserMs[i] * 0.001 * sampleRate));
        const int maxSamples = diffuserDelaySamples[static_cast<size_t>(i)] + 8;
        for (int ch = 0; ch < 2; ++ch)
            diffusers[static_cast<size_t>(ch)][static_cast<size_t>(i)].prepare(maxSamples);
    }

    maxPredelaySamples = static_cast<int>(std::ceil(0.2 * sampleRate)) + 8;
    preDelay[0].prepare(maxPredelaySamples);
    preDelay[1].prepare(maxPredelaySamples);

    size.prepare(sampleRate, 20.0, 50.0f);
    decay.prepare(sampleRate, 30.0, 2.0f);
    predelay.prepare(sampleRate, 20.0, 20.0f);
    damping.prepare(sampleRate, 20.0, 40.0f);
    diffusion.prepare(sampleRate, 20.0, 70.0f);
    width.prepare(sampleRate, 20.0, 100.0f);
    lowCut.prepare(sampleRate, 20.0, 80.0f);
    highCut.prepare(sampleRate, 20.0, 12000.0f);
    mix.prepare(sampleRate, 20.0, 25.0f);

    freezeAmount.reset(sampleRate, 0.02);
    freezeAmount.setCurrentAndTargetValue(0.0f);

    prepared = true;
    reset();
}

void ReverbModule::reset() noexcept
{
    size.snap(size.target());
    decay.snap(decay.target());
    predelay.snap(predelay.target());
    damping.snap(damping.target());
    diffusion.snap(diffusion.target());
    width.snap(width.target());
    lowCut.snap(lowCut.target());
    highCut.snap(highCut.target());
    mix.snap(mix.target());

    freezeAmount.setCurrentAndTargetValue(0.0f);

    for (auto& line : lines)
        line.reset();
    for (auto& line : preDelay)
        line.reset();
    for (auto& chain : diffusers)
        for (auto& ap : chain)
            ap.reset();

    for (auto& filter : lowCutFilter)
        filter.reset();
    for (auto& filter : highCutFilter)
        filter.reset();

    dampingState.fill(0.0);
    delaySamples.fill(0.0f);
    decayGain.fill(0.0f);
    level.store(-120.0f, std::memory_order_relaxed);
}

void ReverbModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (!prepared || n <= 0 || numCh <= 0)
        return;

    size.updateTarget();
    decay.updateTarget();
    predelay.updateTarget();
    damping.updateTarget();
    diffusion.updateTarget();
    width.updateTarget();
    lowCut.updateTarget();
    highCut.updateTarget();
    mix.updateTarget();
    freezeAmount.setTargetValue(freeze.getBool() ? 1.0f : 0.0f);

    auto* left = buffer.getWritePointer(0);
    auto* right = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

    float peak = 0.0f;

    for (int offset = 0; offset < n; offset += subBlock)
    {
        const int c = juce::jmin(subBlock, n - offset);

        const float sizePct = juce::jlimit(0.0f, 1.0f, size.smoother.skip(c) * 0.01f);
        const double rt60 = juce::jmax(0.05, static_cast<double>(decay.smoother.skip(c)));
        const float preSamples = juce::jlimit(
            0.0f, static_cast<float>(maxPredelaySamples - 4),
            predelay.smoother.skip(c) * 0.001f * static_cast<float>(sampleRate));
        const float dampingPct = juce::jlimit(0.0f, 1.0f, damping.smoother.skip(c) * 0.01f);
        const float diffusionPct = juce::jlimit(0.0f, 1.0f, diffusion.smoother.skip(c) * 0.01f);
        const float widthGain = juce::jlimit(0.0f, 2.0f, width.smoother.skip(c) * 0.01f);
        const float mixValue = mixLinear(mix.smoother.skip(c) * 0.01f);
        const float lowHz = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.49),
                                         lowCut.smoother.skip(c));
        const float highHz = juce::jlimit(1000.0f, static_cast<float>(sampleRate * 0.49),
                                          highCut.smoother.skip(c));

        const float roomScale = 0.35f + 1.25f * sizePct;
        for (int i = 0; i < numLines; ++i)
        {
            const float d = baseDelaySamples[static_cast<size_t>(i)] * roomScale;
            delaySamples[static_cast<size_t>(i)] = d;
            const double exponent = -3.0 * static_cast<double>(d) / (rt60 * sampleRate);
            decayGain[static_cast<size_t>(i)] = juce::jlimit(
                0.0f, 0.9995f, static_cast<float>(std::pow(10.0, exponent)));
        }

        const double dampingHz = 20000.0 * std::pow(0.03, static_cast<double>(dampingPct));
        const double dampingCoeff = std::exp(-dsp::twoPi * dampingHz / sampleRate);
        const float diffuserGain = 0.2f + 0.65f * diffusionPct;

        const auto lowCoeffs = dsp::BiquadCoeffs::highPass(sampleRate, lowHz, 0.707);
        const auto highCoeffs = dsp::BiquadCoeffs::lowPass(sampleRate, highHz, 0.707);
        for (int ch = 0; ch < 2; ++ch)
        {
            lowCutFilter[static_cast<size_t>(ch)].set(lowCoeffs);
            highCutFilter[static_cast<size_t>(ch)].set(highCoeffs);
        }

        for (int i = offset; i < offset + c; ++i)
        {
            const float inL = left[i];
            const float inR = right != nullptr ? right[i] : inL;
            const float freezeValue = freezeAmount.getNextValue();

            preDelay[0].push(inL);
            preDelay[1].push(inR);

            float dL = preDelay[0].read(preSamples);
            float dR = preDelay[1].read(preSamples);

            for (int d = 0; d < numDiffusers; ++d)
            {
                const int delay = diffuserDelaySamples[static_cast<size_t>(d)];
                dL = diffusers[0][static_cast<size_t>(d)].process(dL, delay, diffuserGain);
                dR = diffusers[1][static_cast<size_t>(d)].process(dR, delay, diffuserGain);
            }

            const float fdnInput = 0.5f * (dL + dR) * (1.0f - freezeValue);

            std::array<float, numLines> out {};
            for (int line = 0; line < numLines; ++line)
            {
                const float x = lines[static_cast<size_t>(line)].read(
                    delaySamples[static_cast<size_t>(line)]);
                const double damped = (1.0 - dampingCoeff) * static_cast<double>(x)
                                    + dampingCoeff * dampingState[static_cast<size_t>(line)];
                dampingState[static_cast<size_t>(line)] = std::isfinite(damped) ? damped : 0.0;
                out[static_cast<size_t>(line)] =
                    dsp::sanitize(static_cast<float>(dampingState[static_cast<size_t>(line)]));
            }

            const float sideA = 0.5f * (out[0] + out[2] + out[4] + out[6]);
            const float sideB = 0.5f * (out[1] + out[3] + out[5] + out[7]);
            const float mid = 0.5f * (sideA + sideB);
            const float side = 0.5f * (sideA - sideB) * widthGain;

            float wetL = mid + side;
            float wetR = mid - side;
            wetL = static_cast<float>(highCutFilter[0].process(
                lowCutFilter[0].process(static_cast<double>(wetL))));
            wetR = static_cast<float>(highCutFilter[1].process(
                lowCutFilter[1].process(static_cast<double>(wetR))));
            wetL = dsp::sanitize(wetL);
            wetR = dsp::sanitize(wetR);

            std::array<float, numLines> feedback = out;
            detail::hadamard8(feedback);
            for (int line = 0; line < numLines; ++line)
            {
                const float targetGain = decayGain[static_cast<size_t>(line)];
                const float g = targetGain + (0.9995f - targetGain) * freezeValue;
                const float injected = (line % 2 == 0 ? fdnInput : -fdnInput) * 0.35355339f;
                lines[static_cast<size_t>(line)].push(feedback[static_cast<size_t>(line)] * g
                                                      + injected);
            }

            const float dryGain = 1.0f - mixValue;
            const float outL = dsp::sanitize(inL * dryGain + wetL * mixValue);
            const float outR = dsp::sanitize(inR * dryGain + wetR * mixValue);

            left[i] = outL;
            if (right != nullptr)
                right[i] = outR;
            else
                left[i] = dsp::sanitize(0.5f * (outL + outR));

            peak = juce::jmax(peak, std::abs(left[i]),
                              right != nullptr ? std::abs(right[i]) : 0.0f);
        }
    }

    level.store(dsp::gainToDb(peak), std::memory_order_relaxed);
}

//==============================================================================
DelayModule::DelayModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    sync.bind(state, prefix + ".sync");
    division.bind(state, prefix + ".division");
    timeL.bind(state, prefix + ".timeL");
    timeR.bind(state, prefix + ".timeR");
    feedback.bind(state, prefix + ".feedback");
    pingPong.bind(state, prefix + ".pingpong");
    lowCut.bind(state, prefix + ".lowcut");
    highCut.bind(state, prefix + ".highcut");
    mix.bind(state, prefix + ".mix");
}

void DelayModule::prepare(double newSampleRate, int, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, 2, numChannels);

    // 8 s covers 4 beats at 30 BPM (1/1 sync); free time only uses 2 s.
    maxDelaySamples = static_cast<int>(std::ceil(8.0 * sampleRate)) + 8;
    delayL.prepare(maxDelaySamples);
    delayR.prepare(maxDelaySamples);

    delaySamplesL.reset(sampleRate, 0.05);
    delaySamplesR.reset(sampleRate, 0.05);
    delaySamplesL.setCurrentAndTargetValue(1.0f);
    delaySamplesR.setCurrentAndTargetValue(1.0f);

    feedback.prepare(sampleRate, 20.0, 35.0f);
    lowCut.prepare(sampleRate, 20.0, 120.0f);
    highCut.prepare(sampleRate, 20.0, 8000.0f);
    mix.prepare(sampleRate, 20.0, 25.0f);

    prepared = true;
    reset();
}

void DelayModule::reset() noexcept
{
    feedback.snap(feedback.target());
    lowCut.snap(lowCut.target());
    highCut.snap(highCut.target());
    mix.snap(mix.target());

    delayL.reset();
    delayR.reset();

    for (auto& filter : lowCutFilter)
        filter.reset();
    for (auto& filter : highCutFilter)
        filter.reset();

    updateDelayTargets(true);
    level.store(-120.0f, std::memory_order_relaxed);
}

void DelayModule::setTempo(double bpm, bool isPlaying) noexcept
{
    if (std::isfinite(bpm) && bpm > 0.0)
        hostBpm.store(juce::jlimit(20.0f, 400.0f, static_cast<float>(bpm)),
                      std::memory_order_relaxed);
    hostPlaying.store(isPlaying, std::memory_order_relaxed);
}

void DelayModule::updateDelayTargets(bool snap) noexcept
{
    const double beats = detail::beatsForDivision(division.getChoice());
    double targetL = 0.0;
    double targetR = 0.0;

    if (sync.getBool() && beats > 0.0)
    {
        const double bpm = juce::jlimit(20.0, 400.0,
                                        static_cast<double>(hostBpm.load(std::memory_order_relaxed)));
        const double seconds = juce::jmax(0.001, beats * 60.0 / bpm);
        targetL = targetR = seconds * sampleRate;
    }
    else
    {
        targetL = static_cast<double>(timeL.get()) * 0.001 * sampleRate;
        targetR = static_cast<double>(timeR.get()) * 0.001 * sampleRate;
    }

    if (!std::isfinite(targetL) || targetL <= 0.0)
        targetL = 1.0;
    if (!std::isfinite(targetR) || targetR <= 0.0)
        targetR = 1.0;

    const float maxDelay = static_cast<float>(juce::jmax(2, maxDelaySamples - 4));
    const float l = juce::jlimit(1.0f, maxDelay, static_cast<float>(targetL));
    const float r = juce::jlimit(1.0f, maxDelay, static_cast<float>(targetR));

    if (snap)
    {
        delaySamplesL.setCurrentAndTargetValue(l);
        delaySamplesR.setCurrentAndTargetValue(r);
    }
    else
    {
        delaySamplesL.setTargetValue(l);
        delaySamplesR.setTargetValue(r);
    }
}

void DelayModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (!prepared || n <= 0 || numCh <= 0)
        return;

    feedback.updateTarget();
    lowCut.updateTarget();
    highCut.updateTarget();
    mix.updateTarget();
    updateDelayTargets(false);

    auto* left = buffer.getWritePointer(0);
    auto* right = numCh > 1 ? buffer.getWritePointer(1) : nullptr;
    const bool pingPongOn = pingPong.getBool() && right != nullptr;

    float peak = 0.0f;

    for (int offset = 0; offset < n; offset += subBlock)
    {
        const int c = juce::jmin(subBlock, n - offset);

        const float feedbackGain = juce::jlimit(0.0f, 0.95f,
                                                feedback.smoother.skip(c) * 0.01f);
        const float mixValue = mixLinear(mix.smoother.skip(c) * 0.01f);
        const float lowHz = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.49),
                                         lowCut.smoother.skip(c));
        const float highHz = juce::jlimit(1000.0f, static_cast<float>(sampleRate * 0.49),
                                          highCut.smoother.skip(c));

        const auto lowCoeffs = dsp::BiquadCoeffs::highPass(sampleRate, lowHz, 0.707);
        const auto highCoeffs = dsp::BiquadCoeffs::lowPass(sampleRate, highHz, 0.707);
        for (int ch = 0; ch < 2; ++ch)
        {
            lowCutFilter[static_cast<size_t>(ch)].set(lowCoeffs);
            highCutFilter[static_cast<size_t>(ch)].set(highCoeffs);
        }

        for (int i = offset; i < offset + c; ++i)
        {
            const float dL = delaySamplesL.getNextValue();
            const float dR = delaySamplesR.getNextValue();

            const float inL = left[i];
            const float inR = right != nullptr ? right[i] : inL;

            float wetL = delayL.read(dL);
            float wetR = delayR.read(dR);

            wetL = static_cast<float>(highCutFilter[0].process(
                lowCutFilter[0].process(static_cast<double>(wetL))));
            wetR = static_cast<float>(highCutFilter[1].process(
                lowCutFilter[1].process(static_cast<double>(wetR))));
            wetL = dsp::sanitize(wetL);
            wetR = dsp::sanitize(wetR);

            const float feedbackL = pingPongOn ? wetR : wetL;
            const float feedbackR = pingPongOn ? wetL : wetR;

            delayL.push(inL + feedbackGain * feedbackL);
            delayR.push(inR + feedbackGain * feedbackR);

            const float dryGain = 1.0f - mixValue;
            const float outL = dsp::sanitize(inL * dryGain + wetL * mixValue);
            const float outR = dsp::sanitize(inR * dryGain + wetR * mixValue);

            if (right != nullptr)
            {
                left[i] = outL;
                right[i] = outR;
            }
            else
            {
                left[i] = dsp::sanitize(0.5f * (outL + outR));
            }

            peak = juce::jmax(peak, std::abs(left[i]),
                              right != nullptr ? std::abs(right[i]) : 0.0f);
        }
    }

    level.store(dsp::gainToDb(peak), std::memory_order_relaxed);
}
} // namespace aurora
