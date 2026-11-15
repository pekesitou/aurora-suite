#include "Metering/PhaseScope.h"

#include <algorithm>
#include <cmath>

namespace aurora
{
void PhaseScope::prepare(double newSampleRate, int maxBlockSize, int channels)
{
    if (! (newSampleRate >= 8000.0 && newSampleRate <= 768000.0)
        || maxBlockSize <= 0
        || channels <= 0)
    {
        preparedFlag.store(false, std::memory_order_release);
        return;
    }

    sampleRate = newSampleRate;
    preparedChannels = juce::jlimit(1, maxChannels, channels);

    decimation = std::max(1, static_cast<int>(
        std::llround(sampleRate / targetPointsPerSecond)));

    correlationCoeff = std::exp(-1.0 / (sampleRate * correlationTimeSeconds));

    reset();

    preparedFlag.store(true, std::memory_order_release);
}

void PhaseScope::reset() noexcept
{
    fifo.reset();

    pointL.fill(0.0f);
    pointR.fill(0.0f);

    decimationCounter = 0;

    meanL = meanR = 0.0;
    meanLL = meanRR = meanLR = 0.0;

    publishedCorrelation.store(1.0f, std::memory_order_relaxed);
    droppedCount.store(0, std::memory_order_relaxed);
}

void PhaseScope::process(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (! preparedFlag.load(std::memory_order_acquire))
        return;

    const int numSamples = buffer.getNumSamples();
    const int bufferChannels = buffer.getNumChannels();

    if (numSamples <= 0 || bufferChannels <= 0)
        return;

    const int activeChannels = juce::jmin(juce::jmin(preparedChannels, bufferChannels), maxChannels);

    if (activeChannels <= 0)
        return;

    const float* left = buffer.getReadPointer(0);
    const float* right = activeChannels > 1 ? buffer.getReadPointer(1) : left;

    const bool stereo = activeChannels > 1;
    const double c = correlationCoeff;
    const double oneMinusC = 1.0 - c;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = dsp::sanitize(left[i]);
        const float r = stereo ? dsp::sanitize(right[i]) : l;

        if (stereo)
        {
            const double ld = double(l);
            const double rd = double(r);

            meanL = c * meanL + oneMinusC * ld;
            meanR = c * meanR + oneMinusC * rd;
            meanLL = c * meanLL + oneMinusC * (ld * ld);
            meanRR = c * meanRR + oneMinusC * (rd * rd);
            meanLR = c * meanLR + oneMinusC * (ld * rd);
        }

        if (++decimationCounter >= decimation)
        {
            decimationCounter = 0;
            pushPoint(l, r);

            if (stereo)
                publishCorrelation();
        }
    }
}

void PhaseScope::pushPoint(float left, float right) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 + size2 < 1)
    {
        droppedCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    pointL[(size_t) start1] = left;
    pointR[(size_t) start1] = right;
    fifo.finishedWrite(1);
}

void PhaseScope::publishCorrelation() noexcept
{
    const double varianceL = std::max(0.0, meanLL - meanL * meanL);
    const double varianceR = std::max(0.0, meanRR - meanR * meanR);
    const double covariance = meanLR - meanL * meanR;
    const double denominator = std::sqrt(varianceL * varianceR);

    if (! (denominator > 1.0e-18) || ! std::isfinite(covariance))
        return;

    float value = static_cast<float>(covariance / denominator);

    if (! std::isfinite(value))
        value = 0.0f;

    publishedCorrelation.store(juce::jlimit(-1.0f, 1.0f, value),
                               std::memory_order_relaxed);
}

float PhaseScope::correlation() const noexcept
{
    return publishedCorrelation.load(std::memory_order_relaxed);
}

int PhaseScope::readPoints(float* destL, float* destR, int maxPoints) noexcept
{
    if (destL == nullptr || destR == nullptr || maxPoints <= 0)
        return 0;

    const int ready = fifo.getNumReady();
    const int count = juce::jmin(ready, maxPoints);

    if (count <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(count, start1, size1, start2, size2);

    std::copy_n(pointL.data() + start1, size1, destL);
    std::copy_n(pointR.data() + start1, size1, destR);
    std::copy_n(pointL.data() + start2, size2, destL + size1);
    std::copy_n(pointR.data() + start2, size2, destR + size1);

    fifo.finishedRead(size1 + size2);

    return size1 + size2;
}

bool PhaseScope::isReady() const noexcept
{
    return preparedFlag.load(std::memory_order_acquire);
}

std::uint64_t PhaseScope::droppedPoints() const noexcept
{
    return droppedCount.load(std::memory_order_relaxed);
}
} // namespace aurora
