#include "Compressor.h"

#include <cmath>

namespace aurora
{
CompressorModule::CompressorModule(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    threshold.bind(state, prefix + ".threshold");
    ratio.bind(state, prefix + ".ratio");
    attack.bind(state, prefix + ".attack");
    release.bind(state, prefix + ".release");
    knee.bind(state, prefix + ".knee");
    detector.bind(state, prefix + ".detector");
    rmsWindow.bind(state, prefix + ".rmsWindow");
    makeup.bind(state, prefix + ".makeup");
    autoMakeup.bind(state, prefix + ".autoMakeup");
    mix.bind(state, prefix + ".mix");
    scHPF.bind(state, prefix + ".scHPF");
    link.bind(state, prefix + ".link");
}

void CompressorModule::prepare(double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    channels = juce::jlimit(1, 2, numChannels);
    threshold.prepare(sampleRate, 20.0, -18.0f);
    makeup.prepare(sampleRate, 20.0, 0.0f);
    mix.prepare(sampleRate, 20.0, 1.0f);
    reset();
}

void CompressorModule::reset() noexcept
{
    threshold.snap(threshold.target());
    makeup.snap(makeup.target());
    mix.snap(mix.target());
    for (auto& f : scFilter)
        f.reset();
    env[0] = env[1] = 0.0;
    rmsState[0] = rmsState[1] = 0.0;
    grMeter.store(0.0f, std::memory_order_relaxed);
    outputLevel.store(-120.0f, std::memory_order_relaxed);
}

void CompressorModule::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0)
        return;

    threshold.updateTarget();
    makeup.updateTarget();
    mix.updateTarget();

    const double thr = juce::jlimit(-60.0, 0.0, static_cast<double>(threshold.target()));
    const double rat = juce::jmax(1.0, static_cast<double>(ratio.get()));
    const double kn = juce::jlimit(0.0, 40.0, static_cast<double>(knee.get()));
    const double atk = juce::jlimit(0.1, 200.0, static_cast<double>(attack.get()));
    const double rel = juce::jlimit(10.0, 2000.0, static_cast<double>(release.get()));
    const bool rms = detector.getChoice() == 1;
    const double rmsMs = juce::jlimit(1.0, 50.0, static_cast<double>(rmsWindow.get()));
    const double linkAmount = juce::jlimit(0.0, 1.0, static_cast<double>(link.get()) * 0.01);
    const bool autoMk = autoMakeup.getBool();
    const double autoDb = autoMk ? juce::jlimit(0.0, 24.0, -dsp::computeGainReductionDb(0.0, thr, rat, kn)) : 0.0;

    attackCoeff = std::exp(-1.0 / juce::jmax(1.0, sampleRate * atk * 0.001));
    releaseCoeff = std::exp(-1.0 / juce::jmax(1.0, sampleRate * rel * 0.001));
    rmsCoeff = std::exp(-1.0 / juce::jmax(1.0, sampleRate * rmsMs * 0.001));

    const double hpf = juce::jlimit(20.0, 500.0, static_cast<double>(scHPF.get()));
    const auto hpfCoeffs = dsp::BiquadCoeffs::highPass(sampleRate, hpf, 0.7071);
    for (int ch = 0; ch < numCh; ++ch)
        scFilter[ch].set(hpfCoeffs);

    auto* l = buffer.getWritePointer(0);
    auto* r = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

    float peak = 0.0f;
    float worstGr = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = l[i];
        const float dryR = r != nullptr ? r[i] : 0.0f;
        const double scL = std::abs(scFilter[0].process(static_cast<double>(dryL)));
        const double scR = numCh > 1 ? std::abs(scFilter[1].process(static_cast<double>(dryR))) : scL;

        double detL = scL;
        double detR = scR;
        if (rms)
        {
            rmsState[0] = rmsCoeff * rmsState[0] + (1.0 - rmsCoeff) * scL * scL;
            rmsState[1] = rmsCoeff * rmsState[1] + (1.0 - rmsCoeff) * scR * scR;
            detL = std::sqrt(juce::jmax(0.0, rmsState[0]));
            detR = std::sqrt(juce::jmax(0.0, rmsState[1]));
        }

        const double common = juce::jmax(detL, detR);
        detL += linkAmount * (common - detL);
        detR += linkAmount * (common - detR);

        const double coeffL = detL > env[0] ? attackCoeff : releaseCoeff;
        const double coeffR = detR > env[1] ? attackCoeff : releaseCoeff;
        env[0] = coeffL * env[0] + (1.0 - coeffL) * detL;
        env[1] = coeffR * env[1] + (1.0 - coeffR) * detR;

        const double thrNow = static_cast<double>(threshold.next());
        const double grL = dsp::computeGainReductionDb(dsp::gainToDb(env[0]), thrNow, rat, kn);
        const double grR = dsp::computeGainReductionDb(dsp::gainToDb(env[1]), thrNow, rat, kn);
        const double mk = static_cast<double>(makeup.next()) + autoDb;
        const double m = juce::jlimit(0.0, 1.0, static_cast<double>(mix.next()));

        const float wetL = dsp::sanitize(static_cast<float>(static_cast<double>(dryL) * dsp::dbToGain(grL + mk)));
        const float wetR = dsp::sanitize(static_cast<float>(static_cast<double>(dryR) * dsp::dbToGain(grR + mk)));
        const float outL = dryL + static_cast<float>(m) * (wetL - dryL);
        const float outR = dryR + static_cast<float>(m) * (wetR - dryR);

        l[i] = dsp::sanitize(outL);
        if (r != nullptr)
            r[i] = dsp::sanitize(outR);

        peak = juce::jmax(peak, std::abs(l[i]), r != nullptr ? std::abs(r[i]) : 0.0f);
        worstGr = juce::jmin(worstGr, static_cast<float>(juce::jmin(grL, grR)));
    }

    grMeter.store(worstGr, std::memory_order_relaxed);
    outputLevel.store(dsp::gainToDb(peak), std::memory_order_relaxed);
}
} // namespace aurora
