#include "DynamicsCore.h"

#include <cmath>

namespace aurora
{
namespace
{
constexpr double dynButterworthQ = 0.70710678118654752440;

double dynClampFreq(double value, double low, double high) noexcept
{
    if (high <= low)
        return juce::jmax(low, 10.0);
    return juce::jlimit(low, high, value);
}

double dynOnePoleCoeff(double fs, double ms) noexcept
{
    const double tau = juce::jmax(1.0e-4, ms) * 0.001;
    return std::exp(-1.0 / juce::jmax(1.0, fs * tau));
}
} // namespace

//==============================================================================
// De-Esser
//==============================================================================
DeEsserModule::DeEsserModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);

    freq.bind(state, prefix + ".freq");
    threshold.bind(state, prefix + ".threshold");
    ratio.bind(state, prefix + ".ratio");
    attack.bind(state, prefix + ".attack");
    release.bind(state, prefix + ".release");
    range.bind(state, prefix + ".range");
    listen.bind(state, prefix + ".listen");
    mix.bind(state, prefix + ".mix");
}

void DeEsserModule::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, kMaxChannels, numChannels);
    preparedBlockSize = juce::jmax(1, maxBlockSize);

    lowBand.setSize(kMaxChannels, preparedBlockSize, false, true);
    highBand.setSize(kMaxChannels, preparedBlockSize, false, true);
    lowBand.clear();
    highBand.clear();

    freq.prepare(sampleRate, 20.0, 6000.0f);
    threshold.prepare(sampleRate, 20.0, -24.0f);
    ratio.prepare(sampleRate, 20.0, 4.0f);
    attack.prepare(sampleRate, 20.0, 1.0f);
    release.prepare(sampleRate, 20.0, 50.0f);
    range.prepare(sampleRate, 20.0, 12.0f);
    mix.prepare(sampleRate, 20.0, 100.0f);

    gainSmoothCoeff = dynOnePoleCoeff(sampleRate, 0.1);
    listenSmoothCoeff = dynOnePoleCoeff(sampleRate, 5.0);

    reset();
}

void DeEsserModule::reset() noexcept
{
    for (auto& channelFilters : lowpass)
        for (auto& filter : channelFilters)
            filter.reset();

    for (auto& channelFilters : highpass)
        for (auto& filter : channelFilters)
            filter.reset();

    lowBand.clear();
    highBand.clear();

    splitFreq = -1.0;
    envelope = 0.0;
    gainSmooth = 1.0;
    listenSmooth = listen.getBool() ? 1.0 : 0.0;

    freq.snap(freq.target());
    threshold.snap(threshold.target());
    ratio.snap(ratio.target());
    attack.snap(attack.target());
    release.snap(release.target());
    range.snap(range.target());
    mix.snap(mix.target());

    gainReductionDbValue.store(0.0f, std::memory_order_relaxed);
}

void DeEsserModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    if (preparedBlockSize <= 0)
        return;

    const int numSamples = juce::jmin(buffer.getNumSamples(), preparedBlockSize);
    const int numCh = juce::jmin(channels, juce::jmin(kMaxChannels, buffer.getNumChannels()));
    if (numSamples <= 0 || numCh <= 0)
        return;

    freq.updateTarget();
    threshold.updateTarget();
    ratio.updateTarget();
    attack.updateTarget();
    release.updateTarget();
    range.updateTarget();
    mix.updateTarget();

    const double f = dynClampFreq(static_cast<double>(freq.current()), 20.0, sampleRate * 0.45);
    const double thresholdDb = static_cast<double>(threshold.current());
    const double ratioValue = juce::jmax(1.0, static_cast<double>(ratio.current()));
    const double rangeDb = juce::jlimit(0.0, 48.0, static_cast<double>(range.current()));
    const double listenTarget = listen.getBool() ? 1.0 : 0.0;

    attackCoeff = dynOnePoleCoeff(sampleRate, static_cast<double>(attack.current()));
    releaseCoeff = dynOnePoleCoeff(sampleRate, static_cast<double>(release.current()));

    freq.smoother.skip(numSamples);
    threshold.smoother.skip(numSamples);
    ratio.smoother.skip(numSamples);
    attack.smoother.skip(numSamples);
    release.smoother.skip(numSamples);
    range.smoother.skip(numSamples);

    configureSplit(f);
    processSplit(buffer, lowBand, highBand, numSamples, numCh);

    std::array<float*, kMaxChannels> destination {};
    std::array<const float*, kMaxChannels> highData {};
    for (int ch = 0; ch < numCh; ++ch)
    {
        destination[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);
        highData[static_cast<size_t>(ch)] = highBand.getReadPointer(ch);
    }

    float worstReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        double detector = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
            detector = juce::jmax(detector, std::abs(static_cast<double>(highData[static_cast<size_t>(ch)][i])));

        const double envelopeCoeff = detector > envelope ? attackCoeff : releaseCoeff;
        envelope = envelopeCoeff * envelope + (1.0 - envelopeCoeff) * detector;
        if (!std::isfinite(envelope))
            envelope = 0.0;

        const double levelDb = dsp::gainToDb(envelope);
        double reductionDb = dsp::computeGainReductionDb(levelDb, thresholdDb, ratioValue, 0.0);
        reductionDb = juce::jmax(reductionDb, -rangeDb);
        worstReduction = juce::jmin(worstReduction, static_cast<float>(reductionDb));

        const double targetGain = dsp::dbToGain(reductionDb);
        gainSmooth += (targetGain - gainSmooth) * gainSmoothCoeff;
        listenSmooth += (listenTarget - listenSmooth) * listenSmoothCoeff;

        const float m = juce::jlimit(0.0f, 1.0f, mix.next() * 0.01f);
        const double mDouble = static_cast<double>(m);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = destination[static_cast<size_t>(ch)];
            const double dry = static_cast<double>(data[i]);
            const double high = static_cast<double>(highData[static_cast<size_t>(ch)][i]);

            const double deEssed = dry + high * (gainSmooth - 1.0);
            const double listened = high * gainSmooth;

            double output = dry + (deEssed - dry) * mDouble;
            output += (listened - output) * listenSmooth;

            data[i] = dsp::sanitize(static_cast<float>(output));
        }
    }

    gainReductionDbValue.store(worstReduction, std::memory_order_relaxed);
}

void DeEsserModule::configureSplit(double newFreq) noexcept
{
    const double f = juce::jlimit(10.0, sampleRate * 0.45, newFreq);
    if (std::abs(f - splitFreq) < 1.0e-6)
        return;

    const auto lowpassCoeffs = dsp::BiquadCoeffs::lowPass(sampleRate, f, dynButterworthQ);
    const auto highpassCoeffs = dsp::BiquadCoeffs::highPass(sampleRate, f, dynButterworthQ);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        auto& lowStages = lowpass[static_cast<size_t>(ch)];
        auto& highStages = highpass[static_cast<size_t>(ch)];

        for (int stage = 0; stage < 2; ++stage)
        {
            lowStages[static_cast<size_t>(stage)].set(lowpassCoeffs);
            highStages[static_cast<size_t>(stage)].set(highpassCoeffs);
        }
    }

    splitFreq = f;
}

void DeEsserModule::processSplit(const juce::AudioBuffer<float>& source,
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

        auto& lowStages = lowpass[static_cast<size_t>(ch)];
        auto& highStages = highpass[static_cast<size_t>(ch)];

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

//==============================================================================
// Gate / Expander
//==============================================================================
GateModule::GateModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);

    threshold.bind(state, prefix + ".threshold");
    range.bind(state, prefix + ".range");
    attack.bind(state, prefix + ".attack");
    hold.bind(state, prefix + ".hold");
    release.bind(state, prefix + ".release");
    hysteresis.bind(state, prefix + ".hysteresis");
    scHPF.bind(state, prefix + ".scHPF");
    mix.bind(state, prefix + ".mix");
}

void GateModule::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, kMaxChannels, numChannels);
    preparedBlockSize = juce::jmax(1, maxBlockSize);

    threshold.prepare(sampleRate, 20.0, -40.0f);
    range.prepare(sampleRate, 20.0, -60.0f);
    attack.prepare(sampleRate, 20.0, 1.0f);
    hold.prepare(sampleRate, 20.0, 50.0f);
    release.prepare(sampleRate, 20.0, 200.0f);
    hysteresis.prepare(sampleRate, 20.0, 3.0f);
    scHPF.prepare(sampleRate, 20.0, 80.0f);
    mix.prepare(sampleRate, 20.0, 100.0f);

    detectorAttackCoeff = dynOnePoleCoeff(sampleRate, 0.5);
    detectorReleaseCoeff = dynOnePoleCoeff(sampleRate, 20.0);

    reset();
}

void GateModule::reset() noexcept
{
    for (auto& filter : sidechainFilters)
        filter.reset();

    sidechainFreq = -1.0;
    detectorEnvelope = 0.0;
    gateGain = 1.0;
    gateOpen = false;
    holdSamplesRemaining = 0;

    threshold.snap(threshold.target());
    range.snap(range.target());
    attack.snap(attack.target());
    hold.snap(hold.target());
    release.snap(release.target());
    hysteresis.snap(hysteresis.target());
    scHPF.snap(scHPF.target());
    mix.snap(mix.target());

    gainReductionDbValue.store(0.0f, std::memory_order_relaxed);
}

void GateModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    if (preparedBlockSize <= 0)
        return;

    const int numSamples = juce::jmin(buffer.getNumSamples(), preparedBlockSize);
    const int numCh = juce::jmin(channels, juce::jmin(kMaxChannels, buffer.getNumChannels()));
    if (numSamples <= 0 || numCh <= 0)
        return;

    threshold.updateTarget();
    range.updateTarget();
    attack.updateTarget();
    hold.updateTarget();
    release.updateTarget();
    hysteresis.updateTarget();
    scHPF.updateTarget();
    mix.updateTarget();

    const double thresholdDb = static_cast<double>(threshold.current());
    const double rangeDb = juce::jlimit(-80.0, 0.0, static_cast<double>(range.current()));
    const double rangeGain = dsp::dbToGain(rangeDb);
    const double hysteresisDb = juce::jmax(0.0, static_cast<double>(hysteresis.current()));
    const double closeThresholdDb = thresholdDb - hysteresisDb;
    const double holdMs = juce::jmax(0.0, static_cast<double>(hold.current()));
    const int holdSamples = static_cast<int>(std::lround(holdMs * 0.001 * sampleRate));
    const double hpf = dynClampFreq(static_cast<double>(scHPF.current()), 20.0, sampleRate * 0.45);

    const double attackCoeff = dynOnePoleCoeff(sampleRate, static_cast<double>(attack.current()));
    const double releaseCoeff = dynOnePoleCoeff(sampleRate, static_cast<double>(release.current()));

    threshold.smoother.skip(numSamples);
    range.smoother.skip(numSamples);
    attack.smoother.skip(numSamples);
    hold.smoother.skip(numSamples);
    release.smoother.skip(numSamples);
    hysteresis.smoother.skip(numSamples);
    scHPF.smoother.skip(numSamples);

    if (std::abs(hpf - sidechainFreq) > 1.0e-6)
    {
        const auto coeffs = dsp::BiquadCoeffs::highPass(sampleRate, hpf, dynButterworthQ);
        for (auto& filter : sidechainFilters)
            filter.set(coeffs);

        sidechainFreq = hpf;
    }

    std::array<float*, kMaxChannels> data {};
    for (int ch = 0; ch < numCh; ++ch)
        data[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);

    float worstReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        double detector = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const double filtered = sidechainFilters[static_cast<size_t>(ch)].process(
                static_cast<double>(data[static_cast<size_t>(ch)][i]));
            detector = juce::jmax(detector, std::abs(filtered));
        }

        const double envelopeCoeff = detector > detectorEnvelope ? detectorAttackCoeff : detectorReleaseCoeff;
        detectorEnvelope = envelopeCoeff * detectorEnvelope + (1.0 - envelopeCoeff) * detector;
        if (!std::isfinite(detectorEnvelope))
            detectorEnvelope = 0.0;

        const double levelDb = dsp::gainToDb(detectorEnvelope);

        if (gateOpen)
        {
            if (levelDb >= closeThresholdDb)
                holdSamplesRemaining = holdSamples;
            else if (holdSamplesRemaining > 0)
                --holdSamplesRemaining;
            else
                gateOpen = false;
        }
        else if (levelDb >= thresholdDb)
        {
            gateOpen = true;
            holdSamplesRemaining = holdSamples;
        }

        const double targetGain = gateOpen ? 1.0 : rangeGain;
        const double gainCoeff = targetGain > gateGain ? attackCoeff : releaseCoeff;
        gateGain += (targetGain - gateGain) * gainCoeff;
        gateGain = juce::jlimit(0.0, 1.0, gateGain);

        worstReduction = juce::jmin(worstReduction, static_cast<float>(juce::jlimit(-144.0, 0.0, dsp::gainToDb(gateGain))));

        const float m = juce::jlimit(0.0f, 1.0f, mix.next() * 0.01f);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* channelData = data[static_cast<size_t>(ch)];
            const float dry = channelData[i];
            const float wet = dry * static_cast<float>(gateGain);
            channelData[i] = dsp::sanitize(dry + (wet - dry) * m);
        }
    }

    gainReductionDbValue.store(worstReduction, std::memory_order_relaxed);
}

//==============================================================================
// Transient shaper
//==============================================================================
TransientModule::TransientModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);

    attack.bind(state, prefix + ".attack");
    sustain.bind(state, prefix + ".sustain");
    sensitivity.bind(state, prefix + ".sensitivity");
    output.bind(state, prefix + ".output");
    mix.bind(state, prefix + ".mix");
}

void TransientModule::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    channels = juce::jlimit(1, kMaxChannels, numChannels);
    preparedBlockSize = juce::jmax(1, maxBlockSize);

    attack.prepare(sampleRate, 20.0, 0.0f);
    sustain.prepare(sampleRate, 20.0, 0.0f);
    sensitivity.prepare(sampleRate, 20.0, 50.0f);
    output.prepare(sampleRate, 20.0, 0.0f);
    mix.prepare(sampleRate, 20.0, 100.0f);

    fastAttackCoeff = dynOnePoleCoeff(sampleRate, 0.5);
    fastReleaseCoeff = dynOnePoleCoeff(sampleRate, 15.0);
    slowAttackCoeff = dynOnePoleCoeff(sampleRate, 20.0);
    slowReleaseCoeff = dynOnePoleCoeff(sampleRate, 150.0);
    gainSmoothCoeff = dynOnePoleCoeff(sampleRate, 1.0);

    reset();
}

void TransientModule::reset() noexcept
{
    fastEnvelope = 0.0;
    slowEnvelope = 0.0;
    gainSmooth = 1.0;

    attack.snap(attack.target());
    sustain.snap(sustain.target());
    sensitivity.snap(sensitivity.target());
    output.snap(output.target());
    mix.snap(mix.target());
}

void TransientModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    if (preparedBlockSize <= 0)
        return;

    const int numSamples = juce::jmin(buffer.getNumSamples(), preparedBlockSize);
    const int numCh = juce::jmin(channels, juce::jmin(kMaxChannels, buffer.getNumChannels()));
    if (numSamples <= 0 || numCh <= 0)
        return;

    attack.updateTarget();
    sustain.updateTarget();
    sensitivity.updateTarget();
    output.updateTarget();
    mix.updateTarget();

    const double attackAmount = juce::jlimit(-1.0, 1.0, static_cast<double>(attack.current()) * 0.01);
    const double sustainAmount = juce::jlimit(-1.0, 1.0, static_cast<double>(sustain.current()) * 0.01);
    const double sensitivityAmount = juce::jlimit(0.0, 1.0, static_cast<double>(sensitivity.current()) * 0.01);

    attack.smoother.skip(numSamples);
    sustain.smoother.skip(numSamples);
    sensitivity.smoother.skip(numSamples);

    std::array<float*, kMaxChannels> data {};
    for (int ch = 0; ch < numCh; ++ch)
        data[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);

    for (int i = 0; i < numSamples; ++i)
    {
        double detector = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
            detector = juce::jmax(detector, std::abs(static_cast<double>(data[static_cast<size_t>(ch)][i])));

        const double fastCoeff = detector > fastEnvelope ? fastAttackCoeff : fastReleaseCoeff;
        fastEnvelope = fastCoeff * fastEnvelope + (1.0 - fastCoeff) * detector;

        const double slowCoeff = detector > slowEnvelope ? slowAttackCoeff : slowReleaseCoeff;
        slowEnvelope = slowCoeff * slowEnvelope + (1.0 - slowCoeff) * detector;

        if (!std::isfinite(fastEnvelope))
            fastEnvelope = 0.0;
        if (!std::isfinite(slowEnvelope))
            slowEnvelope = 0.0;

        const double denominator = juce::jmax(1.0e-9, juce::jmax(fastEnvelope, slowEnvelope));
        const double difference = (fastEnvelope - slowEnvelope) / denominator;
        const double transientPart = juce::jmax(0.0, difference);
        const double sustainPart = juce::jmax(0.0, -difference);

        const double gainDb = 12.0 * sensitivityAmount
                              * (attackAmount * transientPart + sustainAmount * sustainPart);
        const double targetGain = dsp::dbToGain(gainDb);
        gainSmooth += (targetGain - gainSmooth) * gainSmoothCoeff;

        const float outputGain = dsp::dbToGain(output.next());
        const float m = juce::jlimit(0.0f, 1.0f, mix.next() * 0.01f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* channelData = data[static_cast<size_t>(ch)];
            const float dry = channelData[i];
            const float wet = dry * static_cast<float>(gainSmooth) * outputGain;
            channelData[i] = dsp::sanitize(dry + (wet - dry) * m);
        }
    }
}
} // namespace aurora
