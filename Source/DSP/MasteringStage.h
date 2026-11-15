#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace aurora
{
class MasteringStage final : public ModuleBase
{
public:
    MasteringStage(ModuleId id, juce::AudioProcessorValueTreeState& state);
    ~MasteringStage() override;

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] int latencySamples() const noexcept override { return latency; }
    [[nodiscard]] float gainReductionDb() const noexcept override { return grMeter.load(std::memory_order_relaxed); }
    [[nodiscard]] float outputLevelDb() const noexcept override { return outputLevel.load(std::memory_order_relaxed); }

private:
    static constexpr int oversamplingOrder = 2; // 4x
    static constexpr int lookaheadBaseSamples = 64;

    ParamRef clipperOn, clipperMode, limiterOn;
    SmoothedParam drive, ceiling, release;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> delayBuffer;
    std::vector<float> maxValues;
    std::vector<int> maxIndices;
    int delayWritePos = 0;
    int delayLength = 0;
    int maxHead = 0, maxTail = 0;
    int lookaheadOS = 0;
    int maxOSBlock = 0;
    int latency = 0;
    int channels = 2;
    double sampleRate = 44100.0;
    double currentGain = 1.0;
    double releaseCoeff = 0.0;

    std::atomic<float> grMeter { 0.0f };
    std::atomic<float> outputLevel { -120.0f };
};
} // namespace aurora
