#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace aurora
{
/**
    Fixed 8x oversampled waveshaper (satA / satB).

    Signal path (all non-linear work at sampleRate * 8):
        input -> drive -> model curve + bias -> DC blocker -> auto-gain
              -> 8x -> 1x -> tilt tone -> dry/wet -> output

    Model curves (v is the driven input, b the scaled bias):
        Tube        f(v) = tanh(v * (1 + 0.28 |v|))               asymmetric, mixed even/odd
        Tape        f(v) = v / (1 + 0.42 |v|)                     symmetric compressive knee
        Diode       f(v) = sign(v) * (1 - exp(-1.7 |v|))          exponential rectifying knee
        Transistor  f(v) = 0.62 tanh(1.9 v) + 0.38 v / sqrt(1+v^2) harder odd clip

    Bias is applied as f(v + b) - f(b), so a zero input still maps to zero and
    the following 5 Hz DC blocker removes the dynamically generated DC.

    autoGain estimates the static (memoryless) RMS gain of the current model,
    drive and bias with a fixed 32-point sine probe and compensates it. It is
    not a compressor: the estimate only changes with the non-linearity settings.

    The oversampler is fixed at 8x (three half-band FIR equiripple stages, max
    quality, integer latency). latencySamples() reports the real value returned
    by juce::dsp::Oversampling; the internal dry path is delayed by the same
    amount so the dry/wet mix does not comb-filter.
*/
class SaturatorModule final : public ModuleBase
{
public:
    SaturatorModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return latency; }
    [[nodiscard]] float outputLevelDb() const noexcept override
    {
        return level.load(std::memory_order_relaxed);
    }

    static constexpr int numModels = 4;
    static constexpr int oversamplingFactor = 8;

private:
    juce::dsp::Oversampling<float> oversampling;
    juce::AudioBuffer<float> workBuffer;

    SmoothedParam drive, tone, bias, output, mix;
    ParamRef model, autoGain;

    std::array<dsp::DcBlocker, kMaxChannels> dcBlockers;
    dsp::StereoDelayLine dryLine; // aligns the dry branch with the oversampler latency

    std::array<double, kMaxChannels> toneState { { 0.0, 0.0 } };
    std::array<float, 32> probe {};

    double sampleRate = 44100.0;
    double toneCoeff = 0.0;
    int channels = 2;
    int maxBlock = 0;
    int latency = 0;

    std::atomic<float> level { -120.0f };
};
} // namespace aurora
