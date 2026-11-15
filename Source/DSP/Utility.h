#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

namespace aurora
{
class UtilityModule final : public ModuleBase
{
public:
    UtilityModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return 0; }

private:
    SmoothedParam gain, pan;
    ParamRef polarityL, polarityR, mono, swap;

    double sampleRate = 44100.0;
    int channels = 2;
    std::atomic<float> outputLevel { -120.0f };
};
} // namespace aurora
