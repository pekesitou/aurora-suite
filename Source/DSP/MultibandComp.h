#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

#include <array>
#include <atomic>

namespace aurora
{
/**
    Four-band compressor built on a Linkwitz-Riley 4th order crossover tree.

    The tree is split at x1, x2 and x3. Every split is a cascade of two
    Butterworth (Q = 1/sqrt(2)) biquads, which gives the required
    24 dB/octave LR4 slopes and a flat magnitude reconstruction when all
    bands are summed.

    Each band owns an independent peak detector, envelope, soft-knee gain
    computer, makeup gain, mute/solo gate and gain smoother. Detectors are
    stereo linked (the detector uses the maximum of both channels).
*/
class MultibandComp final : public ModuleBase
{
public:
    MultibandComp(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }

    /** Most negative gain reduction across the four bands, in dB (<= 0). */
    [[nodiscard]] float gainReductionDb() const noexcept override
    {
        return gainReductionDbValue.load(std::memory_order_relaxed);
    }

    static constexpr int numBands = 4;

private:
    static constexpr int maxChannels = kMaxChannels;

    enum ScratchIndex : int
    {
        low1 = 0,
        high1,
        low2,
        high2,
        low3,
        high3
    };

    static constexpr int numScratch = 6;

    struct Band
    {
        ParamRef mute, solo;
        SmoothedParam threshold, ratio, attack, release, knee, makeup;

        double envelope = 0.0;
        double attackCoeff = 0.0;
        double releaseCoeff = 0.0;
        double gainSmooth = 1.0;
        double gainSmoothCoeff = 0.0;
        double gateSmooth = 1.0;
        double gateSmoothCoeff = 0.0;
        float lastGainReductionDb = 0.0f;
    };

    struct LR4Split
    {
        std::array<std::array<dsp::Biquad, 2>, maxChannels> lowpass;
        std::array<std::array<dsp::Biquad, 2>, maxChannels> highpass;
        double lastFreq = -1.0;
    };

    void configureSplit(LR4Split& split, double freq) noexcept;

    void processSplit(LR4Split& split,
                      const juce::AudioBuffer<float>& source,
                      juce::AudioBuffer<float>& lowDestination,
                      juce::AudioBuffer<float>& highDestination,
                      int numSamples,
                      int numChannelsToUse) noexcept;

    void processBand(Band& band,
                     juce::AudioBuffer<float>& bandBuffer,
                     int numSamples,
                     int numChannelsToUse,
                     bool audible) noexcept;

    std::array<Band, numBands> bands;
    std::array<LR4Split, 3> splits;
    std::array<juce::AudioBuffer<float>, numScratch> scratch;

    SmoothedParam x1, x2, x3, mix;

    double sampleRate = 44100.0;
    int channels = 2;
    int preparedBlockSize = 0;

    std::atomic<float> gainReductionDbValue { 0.0f };
};
} // namespace aurora
