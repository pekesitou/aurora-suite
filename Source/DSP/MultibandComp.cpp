#include "MultibandComp.h"

#include <cmath>

namespace aurora
{
namespace
{
constexpr double mbButterworthQ = 0.70710678118654752440;

double mbClampFreq(double value, double low, double high) noexcept
{
    if (high <= low)
        return juce::jmax(low, 10.0);
    return juce::jlimit(low, high, value);
}

double mbOnePoleCoeff(double fs, double ms) noexcept
{
    const double tau = juce::jmax(1.0e-4, ms) * 0.001;
    return std::exp(-1.0 / juce::jmax(1.0, fs * tau));
}
} // namespace

MultibandComp::MultibandComp(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);

    x1.bind(state, prefix + ".x1");
    x2.bind(state, prefix + ".x2");
    x3.bind(state, prefix + ".x3");
    mix.bind(state, prefix + ".mix");

    for (int i = 0; i < numBands; ++i)
    {
        const auto bandName = "b" + juce::String(i + 1);
        auto& band = bands[static_cast<size_t>(i)];

        band.threshold.bind(state, prefix + "." + bandName + ".threshold");
        band.ratio.bind(state, prefix + "." + bandName + ".ratio");
        band.attack.bind(state, prefix + "." + bandName + ".attack");
        band.release.bind(state, prefix + "." + bandName + ".release");
        band.knee.bind(state, prefix + "." + bandName + ".knee");
        band.makeup.bind(state, prefix + "." + bandName + ".makeup");
        band.mute.bind(state, prefix + "." + bandName + ".mute");
        band.solo.bind(state, prefix + "." + bandName + ".solo");
    }
}

void MultibandComp::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, maxChannels, numChannels);
    preparedBlockSize = juce::jmax(1, maxBlockSize);

    for (auto& buffer : scratch)
    {
        buffer.setSize(maxChannels, preparedBlockSize, false, true);
        buffer.clear();
    }

    x1.prepare(sampleRate, 30.0, 120.0f);
    x2.prepare(sampleRate, 30.0, 800.0f);
    x3.prepare(sampleRate, 30.0, 5000.0f);
    mix.prepare(sampleRate, 20.0, 100.0f);

    for (auto& band : bands)
    {
        band.threshold.prepare(sampleRate, 20.0, -18.0f);
        band.ratio.prepare(sampleRate, 20.0, 2.0f);
        band.attack.prepare(sampleRate, 20.0, 10.0f);
        band.release.prepare(sampleRate, 20.0, 100.0f);
        band.knee.prepare(sampleRate, 20.0, 10.0f);
        band.makeup.prepare(sampleRate, 20.0, 0.0f);

        band.gainSmoothCoeff = mbOnePoleCoeff(sampleRate, 2.0);
        band.gateSmoothCoeff = mbOnePoleCoeff(sampleRate, 5.0);
    }

    reset();
}

void MultibandComp::reset() noexcept
{
    for (auto& split : splits)
    {
        for (auto& channelFilters : split.lowpass)
            for (auto& filter : channelFilters)
                filter.reset();

        for (auto& channelFilters : split.highpass)
            for (auto& filter : channelFilters)
                filter.reset();

        split.lastFreq = -1.0;
    }

    for (auto& buffer : scratch)
        buffer.clear();

    bool anySolo = false;
    for (const auto& band : bands)
        anySolo = anySolo || band.solo.getBool();

    for (auto& band : bands)
    {
        band.envelope = 0.0;
        band.gainSmooth = 1.0;
        band.gateSmooth = (!band.mute.getBool() && (!anySolo || band.solo.getBool())) ? 1.0 : 0.0;
        band.lastGainReductionDb = 0.0f;

        band.threshold.snap(band.threshold.target());
        band.ratio.snap(band.ratio.target());
        band.attack.snap(band.attack.target());
        band.release.snap(band.release.target());
        band.knee.snap(band.knee.target());
        band.makeup.snap(band.makeup.target());
    }

    x1.snap(x1.target());
    x2.snap(x2.target());
    x3.snap(x3.target());
    mix.snap(mix.target());

    gainReductionDbValue.store(0.0f, std::memory_order_relaxed);
}

void MultibandComp::process(juce::AudioBuffer<float>& buffer) noexcept
{
    if (preparedBlockSize <= 0)
        return;

    const int numSamples = juce::jmin(buffer.getNumSamples(), preparedBlockSize);
    const int numCh = juce::jmin(channels, juce::jmin(maxChannels, buffer.getNumChannels()));
    if (numSamples <= 0 || numCh <= 0)
        return;

    x1.updateTarget();
    x2.updateTarget();
    x3.updateTarget();

    const double maxF = sampleRate * 0.45;
    const double f1 = mbClampFreq(static_cast<double>(x1.current()), 20.0, maxF * 0.80);
    const double f2 = mbClampFreq(static_cast<double>(x2.current()), f1 * 1.05, maxF * 0.90);
    const double f3 = mbClampFreq(static_cast<double>(x3.current()), f2 * 1.05, maxF);

    x1.smoother.skip(numSamples);
    x2.smoother.skip(numSamples);
    x3.smoother.skip(numSamples);

    configureSplit(splits[0], f1);
    configureSplit(splits[1], f2);
    configureSplit(splits[2], f3);

    processSplit(splits[0], buffer, scratch[static_cast<size_t>(low1)],
                 scratch[static_cast<size_t>(high1)], numSamples, numCh);
    processSplit(splits[1], scratch[static_cast<size_t>(high1)],
                 scratch[static_cast<size_t>(low2)], scratch[static_cast<size_t>(high2)],
                 numSamples, numCh);
    processSplit(splits[2], scratch[static_cast<size_t>(high2)],
                 scratch[static_cast<size_t>(low3)], scratch[static_cast<size_t>(high3)],
                 numSamples, numCh);

    constexpr std::array<int, numBands> bandBuffers { low1, low2, low3, high3 };

    bool anySolo = false;
    for (auto& band : bands)
        anySolo = anySolo || band.solo.getBool();

    for (int i = 0; i < numBands; ++i)
    {
        auto& band = bands[static_cast<size_t>(i)];
        const bool audible = !band.mute.getBool() && (!anySolo || band.solo.getBool());
        processBand(band, scratch[static_cast<size_t>(bandBuffers[static_cast<size_t>(i)])],
                    numSamples, numCh, audible);
    }

    // high1 is no longer needed by the crossover tree, so it becomes the wet sum.
    auto& wet = scratch[static_cast<size_t>(high1)];
    wet.clear(0, numSamples);

    for (int bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        const auto& bandBuffer = scratch[static_cast<size_t>(bandBuffers[static_cast<size_t>(bandIndex)])];
        for (int ch = 0; ch < numCh; ++ch)
            wet.addFrom(ch, 0, bandBuffer, ch, 0, numSamples);
    }

    mix.updateTarget();

    std::array<float*, maxChannels> destination {};
    std::array<const float*, maxChannels> wetSource {};
    for (int ch = 0; ch < numCh; ++ch)
    {
        destination[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);
        wetSource[static_cast<size_t>(ch)] = wet.getReadPointer(ch);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = juce::jlimit(0.0f, 1.0f, mix.next() * 0.01f);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = destination[static_cast<size_t>(ch)];
            const float dry = data[i];
            const float w = wetSource[static_cast<size_t>(ch)][i];
            data[i] = dsp::sanitize(dry + (w - dry) * m);
        }
    }

    float worstReduction = 0.0f;
    for (const auto& band : bands)
        worstReduction = juce::jmin(worstReduction, band.lastGainReductionDb);

    gainReductionDbValue.store(worstReduction, std::memory_order_relaxed);
}

void MultibandComp::configureSplit(LR4Split& split, double freq) noexcept
{
    const double f = juce::jlimit(10.0, sampleRate * 0.45, freq);
    if (std::abs(f - split.lastFreq) < 1.0e-6)
        return;

    const auto lowpassCoeffs = dsp::BiquadCoeffs::lowPass(sampleRate, f, mbButterworthQ);
    const auto highpassCoeffs = dsp::BiquadCoeffs::highPass(sampleRate, f, mbButterworthQ);

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        auto& lowStages = split.lowpass[static_cast<size_t>(ch)];
        auto& highStages = split.highpass[static_cast<size_t>(ch)];

        for (int stage = 0; stage < 2; ++stage)
        {
            lowStages[static_cast<size_t>(stage)].set(lowpassCoeffs);
            highStages[static_cast<size_t>(stage)].set(highpassCoeffs);
        }
    }

    split.lastFreq = f;
}

void MultibandComp::processSplit(LR4Split& split,
                                 const juce::AudioBuffer<float>& source,
                                 juce::AudioBuffer<float>& lowDestination,
                                 juce::AudioBuffer<float>& highDestination,
                                 int numSamples,
                                 int numChannelsToUse) noexcept
{
    for (int ch = 0; ch < numChannelsToUse; ++ch)
    {
        const auto* input = source.getReadPointer(ch);
        auto* low = lowDestination.getWritePointer(ch);
        auto* high = highDestination.getWritePointer(ch);

        auto& lowStages = split.lowpass[static_cast<size_t>(ch)];
        auto& highStages = split.highpass[static_cast<size_t>(ch)];

        for (int i = 0; i < numSamples; ++i)
        {
            const double x = static_cast<double>(input[i]);

            double lowValue = x;
            for (auto& filter : lowStages)
                lowValue = filter.process(lowValue);

            double highValue = x;
            for (auto& filter : highStages)
                highValue = filter.process(highValue);

            low[i] = dsp::sanitize(static_cast<float>(lowValue));
            high[i] = dsp::sanitize(static_cast<float>(highValue));
        }
    }
}

void MultibandComp::processBand(Band& band,
                                juce::AudioBuffer<float>& bandBuffer,
                                int numSamples,
                                int numChannelsToUse,
                                bool audible) noexcept
{
    band.threshold.updateTarget();
    band.ratio.updateTarget();
    band.attack.updateTarget();
    band.release.updateTarget();
    band.knee.updateTarget();
    band.makeup.updateTarget();

    const double thresholdDb = static_cast<double>(band.threshold.current());
    const double ratio = juce::jmax(1.0, static_cast<double>(band.ratio.current()));
    const double kneeDb = juce::jmax(0.0, static_cast<double>(band.knee.current()));
    const double makeupGain = dsp::dbToGain(static_cast<double>(band.makeup.current()));

    band.attackCoeff = mbOnePoleCoeff(sampleRate, static_cast<double>(band.attack.current()));
    band.releaseCoeff = mbOnePoleCoeff(sampleRate, static_cast<double>(band.release.current()));

    band.threshold.smoother.skip(numSamples);
    band.ratio.smoother.skip(numSamples);
    band.attack.smoother.skip(numSamples);
    band.release.smoother.skip(numSamples);
    band.knee.smoother.skip(numSamples);
    band.makeup.smoother.skip(numSamples);

    const double gateTarget = audible ? 1.0 : 0.0;
    float worstReduction = 0.0f;
    std::array<float*, maxChannels> data {};
    for (int ch = 0; ch < numChannelsToUse; ++ch)
        data[static_cast<size_t>(ch)] = bandBuffer.getWritePointer(ch);

    for (int i = 0; i < numSamples; ++i)
    {
        double detector = 0.0;
        for (int ch = 0; ch < numChannelsToUse; ++ch)
        {
            const auto sample = static_cast<double>(data[static_cast<size_t>(ch)][i]);
            detector = juce::jmax(detector, std::abs(sample));
        }

        const double envelopeCoeff = detector > band.envelope ? band.attackCoeff : band.releaseCoeff;
        band.envelope = envelopeCoeff * band.envelope + (1.0 - envelopeCoeff) * detector;
        if (!std::isfinite(band.envelope))
            band.envelope = 0.0;

        const double levelDb = dsp::gainToDb(band.envelope);
        const double reductionDb = dsp::computeGainReductionDb(levelDb, thresholdDb, ratio, kneeDb);
        worstReduction = juce::jmin(worstReduction, static_cast<float>(reductionDb));

        const double targetGain = dsp::dbToGain(reductionDb) * makeupGain;
        band.gainSmooth += (targetGain - band.gainSmooth) * band.gainSmoothCoeff;
        band.gateSmooth += (gateTarget - band.gateSmooth) * band.gateSmoothCoeff;

        const float gain = dsp::sanitize(static_cast<float>(band.gainSmooth * band.gateSmooth));
        for (int ch = 0; ch < numChannelsToUse; ++ch)
        {
            auto* channelData = data[static_cast<size_t>(ch)];
            channelData[i] = dsp::sanitize(channelData[i] * gain);
        }
    }

    band.lastGainReductionDb = worstReduction;
}
} // namespace aurora
