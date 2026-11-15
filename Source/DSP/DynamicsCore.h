#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

#include <array>
#include <atomic>

namespace aurora
{
/**
    De-Esser: LR4 split at the selected sibilance frequency, a stereo-linked
    peak compressor acting only on the high band and a listen (solo high band)
    monitor path. The reduction is applied as a delta over the dry signal so
    the phase of the low band is untouched.
*/
class DeEsserModule final : public ModuleBase
{
public:
    DeEsserModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }

    /** Most negative high-band gain reduction in dB (<= 0). */
    [[nodiscard]] float gainReductionDb() const noexcept override
    {
        return gainReductionDbValue.load(std::memory_order_relaxed);
    }

private:
    void configureSplit(double freq) noexcept;

    void processSplit(const juce::AudioBuffer<float>& source,
                      juce::AudioBuffer<float>& lowDestination,
                      juce::AudioBuffer<float>& highDestination,
                      int numSamples,
                      int numChannelsToUse) noexcept;

    SmoothedParam freq, threshold, ratio, attack, release, range, mix;
    ParamRef listen;

    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> lowpass;
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> highpass;
    juce::AudioBuffer<float> lowBand, highBand;

    double sampleRate = 44100.0;
    int channels = 2;
    int preparedBlockSize = 0;

    double splitFreq = -1.0;
    double envelope = 0.0;
    double attackCoeff = 0.0;
    double releaseCoeff = 0.0;
    double gainSmooth = 1.0;
    double gainSmoothCoeff = 0.0;
    double listenSmooth = 0.0;
    double listenSmoothCoeff = 0.0;

    std::atomic<float> gainReductionDbValue { 0.0f };
};

/**
    Gate / expander with peak detection, hysteresis, hold and a sidechain HPF.
    The sidechain is internal only; no external key input is used.
*/
class GateModule final : public ModuleBase
{
public:
    GateModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }

    /** Current gain reduction in dB (<= 0). */
    [[nodiscard]] float gainReductionDb() const noexcept override
    {
        return gainReductionDbValue.load(std::memory_order_relaxed);
    }

private:
    SmoothedParam threshold, range, attack, hold, release, hysteresis, scHPF, mix;

    std::array<dsp::Biquad, kMaxChannels> sidechainFilters;

    double sampleRate = 44100.0;
    int channels = 2;
    int preparedBlockSize = 0;

    double sidechainFreq = -1.0;
    double detectorEnvelope = 0.0;
    double detectorAttackCoeff = 0.0;
    double detectorReleaseCoeff = 0.0;
    double gateGain = 1.0;

    bool gateOpen = false;
    int holdSamplesRemaining = 0;

    std::atomic<float> gainReductionDbValue { 0.0f };
};

/**
    Transient shaper based on a fast/slow envelope pair. The normalized
    difference between both envelopes drives independent attack and sustain
    gains. Processing is sample based and allocation free.
*/
class TransientModule final : public ModuleBase
{
public:
    TransientModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }

private:
    SmoothedParam attack, sustain, sensitivity, output, mix;

    double sampleRate = 44100.0;
    int channels = 2;
    int preparedBlockSize = 0;

    double fastEnvelope = 0.0;
    double slowEnvelope = 0.0;
    double fastAttackCoeff = 0.0;
    double fastReleaseCoeff = 0.0;
    double slowAttackCoeff = 0.0;
    double slowReleaseCoeff = 0.0;
    double gainSmooth = 1.0;
    double gainSmoothCoeff = 0.0;
};
} // namespace aurora
