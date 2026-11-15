#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

namespace aurora
{
class CompressorModule final : public ModuleBase
{
public:
    CompressorModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return 0; }
    [[nodiscard]] float gainReductionDb() const noexcept override { return grMeter.load(std::memory_order_relaxed); }
    [[nodiscard]] float outputLevelDb() const noexcept override { return outputLevel.load(std::memory_order_relaxed); }

private:
    SmoothedParam threshold, makeup, mix;
    ParamRef ratio, attack, release, knee, detector, rmsWindow, autoMakeup, scHPF, link;

    dsp::Biquad scFilter[2];
    double env[2] { 0.0, 0.0 };
    double rmsState[2] { 0.0, 0.0 };
    double attackCoeff = 0.0, releaseCoeff = 0.0, rmsCoeff = 0.0;
    double sampleRate = 44100.0;
    int channels = 2;
    std::atomic<float> grMeter { 0.0f };
    std::atomic<float> outputLevel { -120.0f };
};
} // namespace aurora
