#pragma once

#include "DSP/ModuleBase.h"
#include "DSP/DspUtils.h"

#include <array>
#include <atomic>

namespace aurora
{
namespace detail
{
/** Fixed capacity, fractional-read mono delay line. Allocates in prepare() only. */
class FractionalDelayLine
{
public:
    void prepare(int maxDelaySamples)
    {
        size = juce::jmax(8, maxDelaySamples + 4);
        buffer.setSize(1, size, false, true, false);
        buffer.clear();
        write = 0;
    }

    void reset() noexcept
    {
        buffer.clear();
        write = 0;
    }

    [[nodiscard]] int getSize() const noexcept { return size; }

    void push(float value) noexcept
    {
        buffer.setSample(0, write, dsp::sanitize(value));
        if (++write >= size)
            write = 0;
    }

    [[nodiscard]] float read(float delaySamples) const noexcept
    {
        const float d = juce::jlimit(0.0f, static_cast<float>(size - 2), delaySamples);
        const int i0 = static_cast<int>(d);
        const float frac = d - static_cast<float>(i0);

        int index0 = write - 1 - i0;
        while (index0 < 0)
            index0 += size;
        int index1 = index0 - 1;
        if (index1 < 0)
            index1 += size;

        const float a = buffer.getSample(0, index0);
        const float b = buffer.getSample(0, index1);
        return a + frac * (b - a);
    }

private:
    juce::AudioBuffer<float> buffer;
    int size = 1;
    int write = 0;
};

/** Schroeder all-pass with an integer delay length. */
class AllPass
{
public:
    void prepare(int maxDelaySamples)
    {
        size = juce::jmax(4, maxDelaySamples + 4);
        buffer.setSize(1, size, false, true, false);
        buffer.clear();
        write = 0;
    }

    void reset() noexcept
    {
        buffer.clear();
        write = 0;
    }

    [[nodiscard]] float process(float x, int delaySamples, float g) noexcept
    {
        const int d = juce::jlimit(1, size - 1, delaySamples);
        int readIndex = write - d;
        if (readIndex < 0)
            readIndex += size;

        const float delayed = buffer.getSample(0, readIndex);
        const float v = x + g * delayed;

        buffer.setSample(0, write, dsp::sanitize(v));
        if (++write >= size)
            write = 0;

        return delayed - g * v;
    }

private:
    juce::AudioBuffer<float> buffer;
    int size = 1;
    int write = 0;
};

/** In-place unitary fast Walsh-Hadamard transform of order 8 (normalised). */
inline void hadamard8(std::array<float, 8>& v) noexcept
{
    for (int len = 1; len < 8; len <<= 1)
    {
        for (int i = 0; i < 8; i += len << 1)
        {
            for (int j = 0; j < len; ++j)
            {
                const float a = v[static_cast<size_t>(i + j)];
                const float b = v[static_cast<size_t>(i + j + len)];
                v[static_cast<size_t>(i + j)] = a + b;
                v[static_cast<size_t>(i + j + len)] = a - b;
            }
        }
    }

    constexpr float norm = 0.3535533905932738f; // 1 / sqrt(8)
    for (auto& x : v)
        x *= norm;
}

/** Beat length for the division choices in the catalogue. -1 means Free. */
[[nodiscard]] inline double beatsForDivision(int index) noexcept
{
    switch (index)
    {
        case 0:  return 4.0;        // 1/1
        case 1:  return 2.0;        // 1/2
        case 2:  return 1.0;        // 1/4
        case 3:  return 0.5;        // 1/8
        case 4:  return 0.25;       // 1/16
        case 5:  return 0.125;      // 1/32
        case 6:  return 1.5;        // 1/4 D
        case 7:  return 0.75;       // 1/8 D
        case 8:  return 2.0 / 3.0;  // 1/4 T
        case 9:  return 1.0 / 3.0;  // 1/8 T
        case 10:
        default: return -1.0;       // Free
    }
}
} // namespace detail

//==============================================================================
/** Pure M/S stereo width, mid/side trim and mono bass. Latency: 0. */
class StereoImagerModule final : public ModuleBase
{
public:
    StereoImagerModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }

    /** Smoothed L/R correlation of the module output, in [-1, 1]. */
    [[nodiscard]] float correlation() const noexcept
    {
        return correlationValue.load(std::memory_order_relaxed);
    }

private:
    SmoothedParam width, midGain, sideGain, bassMonoFreq, bassMonoAmount;
    dsp::Biquad sideLowPass;

    double sampleRate = 44100.0;
    double corrCoeff = 0.0;
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    int channels = 2;

    std::atomic<float> correlationValue { 0.0f };
};

//==============================================================================
/**
    8-line feedback delay network reverb.

    Structure: pre-delay -> four Schroeder all-passes per channel -> mono FDN
    (8 fractional delay lines, unitary normalised Hadamard matrix), one-pole
    damping per line, stereo reconstruction with width, low/high cut and mix.

    Feedback per line is g = 10^(-3 D / (RT60 * fs)), clamped below 0.9995, so
    the loop is always contractive. Freeze mutes the input and uses the same
    clamped feedback, which keeps the tail alive without ever reaching unity.
*/
class ReverbModule final : public ModuleBase
{
public:
    ReverbModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }
    [[nodiscard]] float outputLevelDb() const noexcept override
    {
        return level.load(std::memory_order_relaxed);
    }

    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

private:
    SmoothedParam size, decay, predelay, damping, diffusion, width, lowCut, highCut, mix;
    ParamRef freeze;
    juce::SmoothedValue<float> freezeAmount;

    std::array<detail::FractionalDelayLine, numLines> lines;
    std::array<double, numLines> dampingState {};
    std::array<float, numLines> decayGain {};
    std::array<float, numLines> delaySamples {};
    std::array<float, numLines> baseDelaySamples {};

    std::array<detail::FractionalDelayLine, 2> preDelay;
    std::array<std::array<detail::AllPass, numDiffusers>, 2> diffusers;
    std::array<int, numDiffusers> diffuserDelaySamples {};
    std::array<dsp::Biquad, 2> lowCutFilter;
    std::array<dsp::Biquad, 2> highCutFilter;

    double sampleRate = 44100.0;
    int channels = 2;
    int maxPredelaySamples = 0;
    bool prepared = false;

    std::atomic<float> level { -120.0f };
};

//==============================================================================
/**
    Stereo delay with host tempo sync and filters inside the feedback loop.
    Free time is used when sync is off or the division is Free. Latency: 0.
*/
class DelayModule final : public ModuleBase
{
public:
    DelayModule(ModuleId id, juce::AudioProcessorValueTreeState& state);

    void prepare(double newSampleRate, int maxBlockSize, int numChannels) override;
    void process(juce::AudioBuffer<float>& buffer) noexcept override;
    void reset() noexcept override;
    void setTempo(double bpm, bool isPlaying) noexcept override;

    [[nodiscard]] int latencySamples() const noexcept override { return 0; }
    [[nodiscard]] float outputLevelDb() const noexcept override
    {
        return level.load(std::memory_order_relaxed);
    }

private:
    void updateDelayTargets(bool snap) noexcept;

    SmoothedParam feedback, lowCut, highCut, mix;
    ParamRef sync, division, timeL, timeR, pingPong;

    juce::SmoothedValue<float> delaySamplesL, delaySamplesR;
    detail::FractionalDelayLine delayL, delayR;
    std::array<dsp::Biquad, 2> lowCutFilter;
    std::array<dsp::Biquad, 2> highCutFilter;

    std::atomic<float> hostBpm { 120.0f };
    std::atomic<bool> hostPlaying { false };

    double sampleRate = 44100.0;
    int channels = 2;
    int maxDelaySamples = 0;
    bool prepared = false;

    std::atomic<float> level { -120.0f };
};
} // namespace aurora
