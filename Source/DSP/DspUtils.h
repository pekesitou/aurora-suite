#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace aurora::dsp
{
inline constexpr double pi = 3.14159265358979323846264338327950288;
inline constexpr double twoPi = 2.0 * pi;

[[nodiscard]] inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
[[nodiscard]] inline double dbToGain(double db) noexcept { return std::pow(10.0, db * 0.05); }
[[nodiscard]] inline float gainToDb(float gain) noexcept
{
    return gain > 1.0e-12f ? 20.0f * std::log10(gain) : -240.0f;
}
[[nodiscard]] inline double gainToDb(double gain) noexcept
{
    return gain > 1.0e-12 ? 20.0 * std::log10(gain) : -240.0;
}
[[nodiscard]] inline float sanitize(float x) noexcept
{
    return std::isfinite(x) ? juce::jlimit(-16.0f, 16.0f, x) : 0.0f;
}
[[nodiscard]] inline double sanitize(double x) noexcept
{
    return std::isfinite(x) ? juce::jlimit(-16.0, 16.0, x) : 0.0;
}
[[nodiscard]] inline float softClip(float x) noexcept
{
    return std::tanh(x);
}
[[nodiscard]] inline float smoothStep(float t) noexcept
{
    t = juce::jlimit(0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}
[[nodiscard]] inline float equalPowerDry(float mix) noexcept
{
    return std::cos(juce::jlimit(0.0f, 1.0f, mix) * 0.5f * static_cast<float>(pi));
}
[[nodiscard]] inline float equalPowerWet(float mix) noexcept
{
    return std::sin(juce::jlimit(0.0f, 1.0f, mix) * 0.5f * static_cast<float>(pi));
}

struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    [[nodiscard]] static BiquadCoeffs lowPass(double fs, double f, double q) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, q));
        const double a0 = 1.0 + alpha;
        return { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0,
                 -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    [[nodiscard]] static BiquadCoeffs highPass(double fs, double f, double q) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, q));
        const double a0 = 1.0 + alpha;
        return { (1.0 + cw) * 0.5 / a0, -(1.0 + cw) / a0, (1.0 + cw) * 0.5 / a0,
                 -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    [[nodiscard]] static BiquadCoeffs bandPass(double fs, double f, double q) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, q));
        const double a0 = 1.0 + alpha;
        return { alpha / a0, 0.0, -alpha / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    [[nodiscard]] static BiquadCoeffs notch(double fs, double f, double q) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, q));
        const double a0 = 1.0 + alpha;
        return { 1.0 / a0, -2.0 * cw / a0, 1.0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    [[nodiscard]] static BiquadCoeffs peak(double fs, double f, double q, double gainDb) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, q));
        const double A = std::pow(10.0, gainDb / 40.0);
        const double a0 = 1.0 + alpha / A;
        return { (1.0 + alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha * A) / a0,
                 -2.0 * cw / a0, (1.0 - alpha / A) / a0 };
    }

    [[nodiscard]] static BiquadCoeffs lowShelf(double fs, double f, double q, double gainDb) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double sw = std::sin(w0);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double beta = std::sqrt(A) / juce::jmax(0.0001, q);
        const double a0 = (A + 1.0) + (A - 1.0) * cw + beta * sw;
        return {
            A * ((A + 1.0) - (A - 1.0) * cw + beta * sw) / a0,
            2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
            A * ((A + 1.0) - (A - 1.0) * cw - beta * sw) / a0,
            -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
            ((A + 1.0) + (A - 1.0) * cw - beta * sw) / a0
        };
    }

    [[nodiscard]] static BiquadCoeffs highShelf(double fs, double f, double q, double gainDb) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double sw = std::sin(w0);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double beta = std::sqrt(A) / juce::jmax(0.0001, q);
        const double a0 = (A + 1.0) - (A - 1.0) * cw + beta * sw;
        return {
            A * ((A + 1.0) + (A - 1.0) * cw + beta * sw) / a0,
            -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0,
            A * ((A + 1.0) + (A - 1.0) * cw - beta * sw) / a0,
            2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0,
            ((A + 1.0) - (A - 1.0) * cw - beta * sw) / a0
        };
    }

    [[nodiscard]] static BiquadCoeffs allPass(double fs, double f, double q) noexcept
    {
        const double w0 = twoPi * juce::jlimit(1.0, fs * 0.499, f) / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * juce::jmax(0.0001, q));
        const double a0 = 1.0 + alpha;
        return { (1.0 - alpha) / a0, -2.0 * cw / a0, (1.0 + alpha) / a0,
                 -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }
};

struct Biquad
{
    BiquadCoeffs c {};
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }
    void set(const BiquadCoeffs& nc) noexcept { c = nc; }
    [[nodiscard]] double process(double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        if (!std::isfinite(y))
        {
            reset();
            return 0.0;
        }
        return y;
    }
};

struct OnePole
{
    double z = 0.0;
    double a = 0.0;

    void setTau(double sampleRate, double seconds) noexcept
    {
        a = std::exp(-1.0 / juce::jmax(1.0, sampleRate * juce::jmax(1.0e-6, seconds)));
    }
    void reset() noexcept { z = 0.0; }
    [[nodiscard]] double process(double x) noexcept
    {
        z = (1.0 - a) * x + a * z;
        return z;
    }
};

struct DcBlocker
{
    double x1 = 0.0, y1 = 0.0, r = 0.995;

    void prepare(double sampleRate) noexcept
    {
        r = std::exp(-twoPi * 5.0 / sampleRate);
        x1 = y1 = 0.0;
    }
    [[nodiscard]] double process(double x) noexcept
    {
        const double y = x - x1 + r * y1;
        x1 = x;
        y1 = std::isfinite(y) ? y : 0.0;
        return y1;
    }
    void reset() noexcept { x1 = y1 = 0.0; }
};

// Fixed capacity, integer-delay stereo line. Allocation happens only in prepare().
class StereoDelayLine
{
public:
    void prepare(int maxDelaySamples, int channels, int maxBlockSize)
    {
        jassert(maxDelaySamples > 0 && channels > 0);
        numChannels = channels;
        capacity = juce::jmax(4, maxDelaySamples + maxBlockSize + 4);
        storage.setSize(numChannels, capacity);
        storage.clear();
        writePos = 0;
        reads.setSize(numChannels, maxBlockSize);
    }

    void reset() noexcept
    {
        storage.clear();
        writePos = 0;
    }

    [[nodiscard]] int getCapacity() const noexcept { return capacity; }

    [[nodiscard]] float read(int channel, int delaySamples) const noexcept
    {
        const int d = juce::jlimit(0, capacity - 1, delaySamples);
        int index = writePos - d;
        if (index < 0)
            index += capacity;
        return storage.getSample(channel, index);
    }

    [[nodiscard]] float readFractional(int channel, float delaySamples) const noexcept
    {
        const float d = juce::jlimit(0.0f, static_cast<float>(capacity - 2), delaySamples);
        const int i0 = static_cast<int>(d);
        const float frac = d - static_cast<float>(i0);
        int index = writePos - i0 - 1;
        while (index < 0)
            index += capacity;
        const int index2 = (index + 1) % capacity;
        const float a = storage.getSample(channel, index);
        const float b = storage.getSample(channel, index2);
        return a + frac * (b - a);
    }

    void push(int channel, float value) noexcept
    {
        storage.setSample(channel, writePos, sanitize(value));
    }

    void advance() noexcept
    {
        if (++writePos >= capacity)
            writePos = 0;
    }

    void pushFrame(const float* const* input, int numChannelsToWrite) noexcept
    {
        for (int ch = 0; ch < numChannelsToWrite; ++ch)
            push(ch, input[ch] != nullptr ? *input[ch] : 0.0f);
    }

private:
    juce::AudioBuffer<float> storage, reads;
    int capacity = 0, numChannels = 0, writePos = 0;
};

struct EnvFollower
{
    double env = 0.0;
    double attackCoeff = 0.0, releaseCoeff = 0.0;

    void prepare(double sampleRate, double attackMs, double releaseMs) noexcept
    {
        attackCoeff = std::exp(-1.0 / juce::jmax(1.0, sampleRate * attackMs * 0.001));
        releaseCoeff = std::exp(-1.0 / juce::jmax(1.0, sampleRate * releaseMs * 0.001));
        env = 0.0;
    }
    void reset() noexcept { env = 0.0; }
    [[nodiscard]] double process(double x) noexcept
    {
        const double rect = std::abs(x);
        const double coeff = rect > env ? attackCoeff : releaseCoeff;
        env = coeff * env + (1.0 - coeff) * rect;
        if (!std::isfinite(env))
            env = 0.0;
        return env;
    }
};

// Soft-knee gain computer in dB. Returns gain reduction in dB (<= 0).
[[nodiscard]] inline double computeGainReductionDb(double inputDb, double thresholdDb,
                                                   double ratio, double kneeDb) noexcept
{
    const double r = juce::jmax(1.0, ratio);
    const double over = inputDb - thresholdDb;
    if (kneeDb > 0.0 && 2.0 * std::abs(over) <= kneeDb)
    {
        const double t = over + kneeDb * 0.5;
        return (1.0 / r - 1.0) * t * t / (2.0 * kneeDb);
    }
    if (over > 0.0)
        return over * (1.0 / r - 1.0);
    return 0.0;
}

[[nodiscard]] inline int nextPowerOfTwo(int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace aurora::dsp
