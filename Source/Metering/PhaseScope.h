#pragma once

#include "DSP/DspUtils.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace aurora
{
/**
    Stereo phase scope.

    Audio thread:
      process() reads L/R, updates an exponential correlation estimate
      (tau ~ 300 ms) and writes decimated Lissajous points into a fixed SPSC
      FIFO (juce::AbstractFifo, 1024 points). Decimation targets 1000 points/s
      at any sample rate. No allocation, no locks, noexcept.

    UI thread:
      readPoints() drains the FIFO without blocking. If the UI is slower than
      the producer the FIFO fills and new points are dropped (droppedPoints).
      correlation() is an atomic snapshot in -1..+1.

    Correlation uses exponential means of l, r, l*l, r*r and l*r, so it is
    DC-independent:
        r = (E[lr] - E[l]E[r]) / sqrt((E[l2] - E[l]^2)(E[r2] - E[r]^2))
    It is only published while the variance is non-negligible; during digital
    silence the last value is kept. Mono input reports +1.
*/
class PhaseScope final
{
public:
    static constexpr int fifoCapacity = 1024;
    static constexpr int maxChannels = 8;
    static constexpr double correlationTimeSeconds = 0.3;
    static constexpr double targetPointsPerSecond = 1000.0;

    PhaseScope() = default;
    ~PhaseScope() = default;

    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;
    PhaseScope(PhaseScope&&) = delete;
    PhaseScope& operator=(PhaseScope&&) = delete;

    // Not RT-safe (it is allocation-free, but must not race with process()).
    void prepare(double sampleRate, int maxBlockSize, int channels);

    // Audio callback. noexcept, allocation-free, lock-free.
    void process(const juce::AudioBuffer<float>& buffer) noexcept;

    // Clears the FIFO, correlation state and counters. RT-safe, but the caller
    // must not call it concurrently with readPoints().
    void reset() noexcept;

    [[nodiscard]] float correlation() const noexcept;

    // UI thread only. Returns the number of points written (<= maxPoints).
    int readPoints(float* destL, float* destR, int maxPoints) noexcept;

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] std::uint64_t droppedPoints() const noexcept;

private:
    void pushPoint(float left, float right) noexcept;
    void publishCorrelation() noexcept;

    juce::AbstractFifo fifo { fifoCapacity };
    std::array<float, fifoCapacity> pointL {};
    std::array<float, fifoCapacity> pointR {};

    double sampleRate = 48000.0;
    int preparedChannels = 2;
    int decimation = 48;
    int decimationCounter = 0;

    double correlationCoeff = 0.0;
    double meanL = 0.0, meanR = 0.0;
    double meanLL = 0.0, meanRR = 0.0, meanLR = 0.0;

    std::atomic<float> publishedCorrelation { 1.0f };
    std::atomic<std::uint64_t> droppedCount { 0 };
    std::atomic<bool> preparedFlag { false };
};
} // namespace aurora
