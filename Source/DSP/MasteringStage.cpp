#include "MasteringStage.h"

#include <cmath>

namespace aurora
{
MasteringStage::MasteringStage(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    clipperOn.bind(state, prefix + ".clipperOn");
    clipperMode.bind(state, prefix + ".clipperMode");
    drive.bind(state, prefix + ".drive");
    ceiling.bind(state, prefix + ".ceiling");
    limiterOn.bind(state, prefix + ".limiterOn");
    release.bind(state, prefix + ".release");
}

MasteringStage::~MasteringStage() = default;

void MasteringStage::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    channels = juce::jlimit(1, 2, numChannels);
    maxOSBlock = juce::jmax(256, maxBlockSize * (1 << oversamplingOrder));
    lookaheadOS = lookaheadBaseSamples * (1 << oversamplingOrder);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(channels), oversamplingOrder,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    oversampler->initProcessing(static_cast<size_t>(maxBlockSize));

    delayLength = lookaheadOS;
    delayBuffer.setSize(channels, delayLength);
    delayBuffer.clear();
    delayWritePos = 0;

    maxValues.assign(static_cast<size_t>(maxOSBlock + lookaheadOS + 8), 0.0f);
    maxIndices.assign(static_cast<size_t>(maxOSBlock + lookaheadOS + 8), 0);

    drive.prepare(sampleRate, 20.0, 0.0f);
    ceiling.prepare(sampleRate, 20.0, -0.3f);
    release.prepare(sampleRate, 20.0, 100.0f);

    latency = static_cast<int>(std::ceil(oversampler->getLatencyInSamples())) + lookaheadBaseSamples;
    reset();
}

void MasteringStage::reset() noexcept
{
    if (oversampler != nullptr)
        oversampler->reset();
    delayBuffer.clear();
    delayWritePos = 0;
    maxHead = maxTail = 0;
    currentGain = 1.0;
    drive.snap(drive.target());
    ceiling.snap(ceiling.target());
    release.snap(release.target());
    grMeter.store(0.0f, std::memory_order_relaxed);
    outputLevel.store(-120.0f, std::memory_order_relaxed);
}

void MasteringStage::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0 || oversampler == nullptr)
        return;

    drive.updateTarget();
    ceiling.updateTarget();
    release.updateTarget();

    const double ceilingDb = juce::jlimit(-24.0, 0.0, static_cast<double>(ceiling.target()));
    const double ceilingLin = juce::jmax(1.0e-6, dsp::dbToGain(ceilingDb - 0.05));
    const double driveGain = dsp::dbToGain(juce::jlimit(0.0, 12.0, static_cast<double>(drive.target())));
    const bool clipper = clipperOn.getBool();
    const bool hardClip = clipperMode.getChoice() == 1;
    const bool limiter = limiterOn.getBool();
    const double relMs = juce::jlimit(10.0, 1000.0, static_cast<double>(release.target()));

    const double osRate = sampleRate * static_cast<double>(1 << oversamplingOrder);
    releaseCoeff = std::exp(-1.0 / juce::jmax(1.0, osRate * relMs * 0.001));

    juce::dsp::AudioBlock<const float> inputBlock(buffer);
    auto osBlock = oversampler->processSamplesUp(inputBlock);
    const int osSamples = static_cast<int>(osBlock.getNumSamples());
    const int osChannels = juce::jmin(numCh, static_cast<int>(osBlock.getNumChannels()));
    if (osSamples <= 0 || osChannels <= 0)
        return;

    jassert(delayBuffer.getNumSamples() == delayLength);
    if (delayBuffer.getNumSamples() != delayLength)
        return;

    maxHead = maxTail = 0;
    int absoluteIndex = 0;
    float peakOut = 0.0f;

    for (int i = 0; i < osSamples; ++i)
    {
        float frame[2] { 0.0f, 0.0f };
        for (int ch = 0; ch < osChannels; ++ch)
            frame[ch] = osBlock.getSample(ch, i);

        if (clipper)
        {
            for (int ch = 0; ch < osChannels; ++ch)
            {
                float x = frame[ch] * static_cast<float>(driveGain);
                if (hardClip)
                    x = juce::jlimit(-static_cast<float>(ceilingLin), static_cast<float>(ceilingLin), x);
                else
                    x = static_cast<float>(ceilingLin) * std::tanh(x / static_cast<float>(ceilingLin));
                frame[ch] = dsp::sanitize(x);
            }
        }

        const float peak = juce::jmax(std::abs(frame[0]), osChannels > 1 ? std::abs(frame[1]) : 0.0f);

        while (maxTail > maxHead && maxValues[static_cast<size_t>(maxTail - 1)] <= peak)
            --maxTail;
        if (maxTail < static_cast<int>(maxValues.size()))
        {
            maxValues[static_cast<size_t>(maxTail)] = peak;
            maxIndices[static_cast<size_t>(maxTail)] = absoluteIndex;
            ++maxTail;
        }
        while (maxHead < maxTail && maxIndices[static_cast<size_t>(maxHead)] <= absoluteIndex - lookaheadOS)
            ++maxHead;

        const double maxPeak = maxHead < maxTail ? static_cast<double>(maxValues[static_cast<size_t>(maxHead)]) : 0.0;
        const double targetGain = limiter && maxPeak > ceilingLin ? ceilingLin / maxPeak : 1.0;
        if (targetGain < currentGain)
            currentGain = targetGain;
        else
            currentGain += (targetGain - currentGain) * (1.0 - releaseCoeff);
        currentGain = juce::jlimit(0.0, 1.0, currentGain);

        for (int ch = 0; ch < osChannels; ++ch)
        {
            const float delayed = delayBuffer.getSample(ch, delayWritePos);
            float out = static_cast<float>(static_cast<double>(delayed) * currentGain);
            if (limiter)
                out = juce::jlimit(-static_cast<float>(ceilingLin), static_cast<float>(ceilingLin), out);
            osBlock.setSample(ch, i, dsp::sanitize(out));
            peakOut = juce::jmax(peakOut, std::abs(out));
        }

        for (int ch = 0; ch < osChannels; ++ch)
            delayBuffer.setSample(ch, delayWritePos, frame[ch]);

        if (++delayWritePos >= delayLength)
            delayWritePos = 0;
        ++absoluteIndex;
    }

    juce::dsp::AudioBlock<float> outputBlock(buffer);
    oversampler->processSamplesDown(outputBlock);

    // Defensive true-peak safety after the decimation filter. Only engages on the
    // rare intersample overshoot that the 4x domain cannot see; the normal path is
    // fully handled by the lookahead limiter.
    const float hardCeiling = static_cast<float>(ceilingLin);
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i)
        {
            float x = data[i];
            if (limiter)
                x = juce::jlimit(-hardCeiling, hardCeiling, x);
            data[i] = dsp::sanitize(x);
            peakOut = juce::jmax(peakOut, std::abs(data[i]));
        }
    }

    grMeter.store(dsp::gainToDb(static_cast<float>(currentGain)), std::memory_order_relaxed);
    outputLevel.store(dsp::gainToDb(peakOut), std::memory_order_relaxed);
}
} // namespace aurora
