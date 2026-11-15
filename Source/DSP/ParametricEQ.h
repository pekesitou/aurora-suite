#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

#include <array>

namespace aurora
{
class ParametricEQ final : public ModuleBase
{
public:
    ParametricEQ(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return 0; }
    [[nodiscard]] float outputLevelDb() const noexcept override { return level.load(std::memory_order_relaxed); }

    static constexpr int numBands = 8;

private:
    struct Band
    {
        ParamRef enabled, type;
        SmoothedParam freq, gain, q;
        std::array<dsp::Biquad, 2> filters;
        int lastType = 4;
        bool lastEnabled = true;
    };

    std::array<Band, numBands> bands;
    SmoothedParam outputGain;
    std::atomic<float> level { -120.0f };

    double sampleRate = 44100.0;
    int channels = 2;
};
} // namespace aurora
