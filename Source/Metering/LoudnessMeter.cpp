#include "Metering/LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace aurora
{
namespace
{
    constexpr double integratedAbsoluteGateLufs = -70.0;
    constexpr double integratedRelativeGateLu = -10.0;
    constexpr double lraAbsoluteGateLufs = -70.0;
    constexpr double lraRelativeGateLu = -20.0;
    constexpr double lufsOffset = -0.691;

    // The channel weight of BS.1770 is 1.0 for L/R/C and 1.41 for surrounds.
    // The rack is stereo-only (docs/01_ARCHITECTURE.md), so every measured
    // channel is weighted 1.0. Keep this in one place if surround is added.
    constexpr double channelWeight = 1.0;
} // namespace

//==============================================================================
void LoudnessMeter::TruePeakOversampler::prepare() noexcept
{
    // Ideal interpolation kernel: h[m] = sinc(0.25 * (m - center)) * Blackman(m).
    // The 4x sample at phase p uses h[p], h[p + 4], ... and is therefore a
    // standard polyphase decomposition of a 128-tap low-pass at 1/8 of the 4x
    // rate (i.e. the original Nyquist). Each phase is normalised to unity DC
    // gain so that the interpolator does not alter the signal level.
    constexpr int L = numTaps;
    constexpr double center = double(L - 1) * 0.5;

    std::array<double, L> kernel {};

    for (int m = 0; m < L; ++m)
    {
        const double t = double(m) - center;
        const double x = dsp::pi * 0.25 * t;
        const double sinc = std::abs(x) < 1.0e-9 ? 1.0 : std::sin(x) / x;
        const double phase = dsp::twoPi * double(m) / double(L - 1);
        const double window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
        kernel[(size_t) m] = sinc * window;
    }

    for (int p = 0; p < factor; ++p)
    {
        double sum = 0.0;
        for (int q = 0; q < tapsPerPhase; ++q)
            sum += kernel[(size_t) (p + factor * q)];

        const double norm = std::abs(sum) > 1.0e-12 ? 1.0 / sum : 1.0;
        for (int q = 0; q < tapsPerPhase; ++q)
            coeffs[(size_t) (p + factor * q)] =
                static_cast<float>(kernel[(size_t) (p + factor * q)] * norm);
    }

    reset();
}

void LoudnessMeter::TruePeakOversampler::reset() noexcept
{
    for (auto& h : history)
        h.fill(0.0f);

    writePos = 0;
}

float LoudnessMeter::TruePeakOversampler::processSample(int channel, float x) noexcept
{
    if (channel < 0 || channel >= maxChannels)
        return 0.0f;

    auto& h = history[(size_t) channel];
    h[(size_t) writePos] = x;

    float peak = 0.0f;

    for (int p = 0; p < factor; ++p)
    {
        float acc = 0.0f;
        int index = writePos;

        for (int q = 0; q < tapsPerPhase; ++q)
        {
            acc += coeffs[(size_t) (p + factor * q)] * h[(size_t) index];
            index = (index - 1) & (historySize - 1);
        }

        const float magnitude = std::abs(acc);
        if (magnitude > peak)
            peak = magnitude;
    }

    return peak;
}

void LoudnessMeter::TruePeakOversampler::advance() noexcept
{
    writePos = (writePos + 1) & (historySize - 1);
}

//==============================================================================
void LoudnessMeter::prepare(double newSampleRate, int maxBlockSize, int channels)
{
    if (! (newSampleRate >= 8000.0 && newSampleRate <= 768000.0)
        || maxBlockSize <= 0
        || channels <= 0)
    {
        preparedFlag.store(false, std::memory_order_release);
        return;
    }

    sampleRate = newSampleRate;
    numChannels = juce::jlimit(1, maxChannels, channels);

    samplesPerSubBlock = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::llround(sampleRate * 0.1)));

    computeKWeightingCoefficients();

    for (auto& chain : filters)
    {
        chain.shelf.set(shelfCoeffs);
        chain.highPass.set(highPassCoeffs);
    }

    truePeak.prepare();

    integratedBlocks.clear();
    integratedBlocks.reserve(static_cast<size_t>(integratedCapacity));

    // One-pole mean used by the L/R correlation. The means are updated once per
    // 100 ms sub-block, so the coefficient must use that 100 ms step to keep
    // the intended ~300 ms time constant.
    correlationCoeff = std::exp(
        -(double(samplesPerSubBlock) / sampleRate) / 0.3);

    reset();

    preparedFlag.store(true, std::memory_order_release);
}

void LoudnessMeter::computeKWeightingCoefficients() noexcept
{
    const double fs = sampleRate;

    // Stage 1: ITU-R BS.1770 pre-filter (high shelf).
    {
        constexpr double f0 = 1681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double q = 0.7071752369554196;

        const double k = std::tan(dsp::pi * f0 / fs);
        const double vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;

        shelfCoeffs.b0 = (vh + vb * k / q + k * k) / a0;
        shelfCoeffs.b1 = 2.0 * (k * k - vh) / a0;
        shelfCoeffs.b2 = (vh - vb * k / q + k * k) / a0;
        shelfCoeffs.a1 = 2.0 * (k * k - 1.0) / a0;
        shelfCoeffs.a2 = (1.0 - k / q + k * k) / a0;
    }

    // Stage 2: RLB high-pass.
    {
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;

        const double k = std::tan(dsp::pi * f0 / fs);
        const double a0 = 1.0 + k / q + k * k;

        // BS.1770 RLB numerator is exactly (1, -2, 1); only the denominator is
        // normalised, matching the coefficients published in ITU-R BS.1770-4.
        highPassCoeffs.b0 = 1.0;
        highPassCoeffs.b1 = -2.0;
        highPassCoeffs.b2 = 1.0;
        highPassCoeffs.a1 = 2.0 * (k * k - 1.0) / a0;
        highPassCoeffs.a2 = (1.0 - k / q + k * k) / a0;
    }
}

void LoudnessMeter::reset() noexcept
{
    for (auto& chain : filters)
    {
        chain.shelf.set(shelfCoeffs);
        chain.shelf.reset();
        chain.highPass.set(highPassCoeffs);
        chain.highPass.reset();
    }

    truePeak.reset();

    subBlockPosition = 0;
    subBlockWeightedEnergy = 0.0;
    subBlockRmsEnergy = 0.0;
    subBlockL = subBlockR = 0.0;
    subBlockLL = subBlockRR = subBlockLR = 0.0;
    subBlockCounter = 0;

    momentaryRing.fill(0.0);
    momentaryIndex = 0;
    momentaryCount = 0;

    shortTermRing.fill(0.0);
    shortTermIndex = 0;
    shortTermCount = 0;

    rmsRing.fill(0.0);
    rmsIndex = 0;
    rmsCount = 0;

    integratedBlocks.clear();

    lraBlocks.fill(0.0);
    lraScratch.fill(0.0);
    lraCount = 0;
    lraWriteIndex = 0;

    maxTruePeak = 0.0;
    maxSamplePeak = 0.0;
    peakResetRequested.store(false, std::memory_order_relaxed);

    corrMeanL = corrMeanR = 0.0;
    corrMeanLL = corrMeanRR = corrMeanLR = 0.0;

    publishedMomentary.store(silenceFloorLufs, std::memory_order_relaxed);
    publishedShortTerm.store(silenceFloorLufs, std::memory_order_relaxed);
    publishedIntegrated.store(silenceFloorLufs, std::memory_order_relaxed);
    publishedLra.store(0.0f, std::memory_order_relaxed);
    publishedTruePeak.store(silenceFloorDb, std::memory_order_relaxed);
    publishedSamplePeak.store(silenceFloorDb, std::memory_order_relaxed);
    publishedRms.store(silenceFloorDb, std::memory_order_relaxed);
    publishedCorrelation.store(1.0f, std::memory_order_relaxed);
    publishedProcessedSamples.store(0, std::memory_order_relaxed);
}

void LoudnessMeter::resetPeakHold() noexcept
{
    peakResetRequested.store(true, std::memory_order_relaxed);
}

void LoudnessMeter::process(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (! preparedFlag.load(std::memory_order_acquire))
        return;

    const int numSamples = buffer.getNumSamples();
    const int bufferChannels = buffer.getNumChannels();

    if (numSamples <= 0 || bufferChannels <= 0)
        return;

    const int activeChannels = juce::jmin(juce::jmin(numChannels, bufferChannels), maxChannels);

    if (activeChannels <= 0)
        return;

    if (peakResetRequested.exchange(false, std::memory_order_relaxed))
    {
        maxTruePeak = 0.0;
        maxSamplePeak = 0.0;
        publishedTruePeak.store(silenceFloorDb, std::memory_order_relaxed);
        publishedSamplePeak.store(silenceFloorDb, std::memory_order_relaxed);
    }

    std::array<const float*, maxChannels> input {};
    for (int ch = 0; ch < activeChannels; ++ch)
        input[(size_t) ch] = buffer.getReadPointer(ch);

    float blockTruePeak = 0.0f;
    float blockSamplePeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        double weightedEnergy = 0.0;
        double rawEnergy = 0.0;
        float left = 0.0f;
        float right = 0.0f;

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            const float x = dsp::sanitize(input[(size_t) ch][i]);

            const float tp = truePeak.processSample(ch, x);
            if (tp > blockTruePeak)
                blockTruePeak = tp;

            const float magnitude = std::abs(x);
            if (magnitude > blockSamplePeak)
                blockSamplePeak = magnitude;

            const double y = filters[(size_t) ch].highPass.process(
                filters[(size_t) ch].shelf.process(double(x)));

            weightedEnergy += y * y;
            rawEnergy += double(x) * double(x);

            if (ch == 0)
                left = x;
            else if (ch == 1)
                right = x;
        }

        truePeak.advance();

        subBlockWeightedEnergy += weightedEnergy;
        subBlockRmsEnergy += rawEnergy;
        subBlockL += double(left);
        subBlockR += double(right);
        subBlockLL += double(left) * double(left);
        subBlockRR += double(right) * double(right);
        subBlockLR += double(left) * double(right);

        if (++subBlockPosition >= samplesPerSubBlock)
            finishSubBlock();
    }

    if (blockTruePeak > maxTruePeak)
        maxTruePeak = double(blockTruePeak);

    if (blockSamplePeak > maxSamplePeak)
        maxSamplePeak = double(blockSamplePeak);

    publishedTruePeak.store(amplitudeToDb(maxTruePeak), std::memory_order_relaxed);
    publishedSamplePeak.store(amplitudeToDb(maxSamplePeak), std::memory_order_relaxed);
    publishedProcessedSamples.fetch_add(
        static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
}

void LoudnessMeter::finishSubBlock() noexcept
{
    const double norm = 1.0 / double(samplesPerSubBlock);
    const double blockEnergy =
        subBlockWeightedEnergy / (double(samplesPerSubBlock) * double(numChannels) * channelWeight);
    const double meanSquare =
        subBlockRmsEnergy / (double(samplesPerSubBlock) * double(numChannels) * channelWeight);

    momentaryRing[(size_t) momentaryIndex] = blockEnergy;
    momentaryIndex = (momentaryIndex + 1) % momentarySubBlocks;
    if (momentaryCount < momentarySubBlocks)
        ++momentaryCount;

    shortTermRing[(size_t) shortTermIndex] = blockEnergy;
    shortTermIndex = (shortTermIndex + 1) % shortTermSubBlocks;
    if (shortTermCount < shortTermSubBlocks)
        ++shortTermCount;

    rmsRing[(size_t) rmsIndex] = meanSquare;
    rmsIndex = (rmsIndex + 1) % momentarySubBlocks;
    if (rmsCount < momentarySubBlocks)
        ++rmsCount;

    if (momentaryCount == momentarySubBlocks)
    {
        double sum = 0.0;
        for (double e : momentaryRing)
            sum += e;

        const double energy = sum / double(momentarySubBlocks);
        publishedMomentary.store(
            juce::jmax(silenceFloorLufs, static_cast<float>(energyToLufs(energy))),
            std::memory_order_relaxed);

        if (integratedBlocks.size() < static_cast<size_t>(integratedCapacity))
            integratedBlocks.push_back(energy);

        updateIntegrated();
    }

    if (shortTermCount == shortTermSubBlocks)
    {
        double sum = 0.0;
        for (double e : shortTermRing)
            sum += e;

        const double energy = sum / double(shortTermSubBlocks);
        publishedShortTerm.store(
            juce::jmax(silenceFloorLufs, static_cast<float>(energyToLufs(energy))),
            std::memory_order_relaxed);

        // 3 s window advancing 1 s: one LRA block per ten 100 ms sub-blocks.
        if (subBlockCounter % 10 == 0)
        {
            lraBlocks[(size_t) lraWriteIndex] = energy;
            lraWriteIndex = (lraWriteIndex + 1) % lraCapacity;
            if (lraCount < lraCapacity)
                ++lraCount;

            updateLra();
        }

        ++subBlockCounter;
    }

    if (rmsCount == momentarySubBlocks)
    {
        double sum = 0.0;
        for (double e : rmsRing)
            sum += e;

        const double mean = sum / double(momentarySubBlocks);
        const float db = mean > 1.0e-18
            ? static_cast<float>(10.0 * std::log10(mean))
            : silenceFloorDb;

        publishedRms.store(juce::jmax(silenceFloorDb, db), std::memory_order_relaxed);
    }

    if (numChannels >= 2)
    {
        const double meanL = subBlockL * norm;
        const double meanR = subBlockR * norm;

        const double c = correlationCoeff;
        const double oneMinusC = 1.0 - c;

        corrMeanL = c * corrMeanL + oneMinusC * meanL;
        corrMeanR = c * corrMeanR + oneMinusC * meanR;
        corrMeanLL = c * corrMeanLL + oneMinusC * (subBlockLL * norm);
        corrMeanRR = c * corrMeanRR + oneMinusC * (subBlockRR * norm);
        corrMeanLR = c * corrMeanLR + oneMinusC * (subBlockLR * norm);

        const double varL = juce::jmax(0.0, corrMeanLL - corrMeanL * corrMeanL);
        const double varR = juce::jmax(0.0, corrMeanRR - corrMeanR * corrMeanR);
        const double covariance = corrMeanLR - corrMeanL * corrMeanR;
        const double denominator = std::sqrt(varL * varR);

        float corr = 1.0f;
        if (denominator > 1.0e-18 && std::isfinite(covariance))
            corr = static_cast<float>(covariance / denominator);

        if (! std::isfinite(corr))
            corr = 0.0f;

        publishedCorrelation.store(juce::jlimit(-1.0f, 1.0f, corr),
                                   std::memory_order_relaxed);
    }

    // Denormal flush for the biquad states (cheap, once per 100 ms).
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& chain = filters[(size_t) ch];

        if (std::abs(chain.shelf.z1) < 1.0e-30)   chain.shelf.z1 = 0.0;
        if (std::abs(chain.shelf.z2) < 1.0e-30)   chain.shelf.z2 = 0.0;
        if (std::abs(chain.highPass.z1) < 1.0e-30) chain.highPass.z1 = 0.0;
        if (std::abs(chain.highPass.z2) < 1.0e-30) chain.highPass.z2 = 0.0;
    }

    subBlockPosition = 0;
    subBlockWeightedEnergy = 0.0;
    subBlockRmsEnergy = 0.0;
    subBlockL = subBlockR = 0.0;
    subBlockLL = subBlockRR = subBlockLR = 0.0;
}

void LoudnessMeter::updateIntegrated() noexcept
{
    const double absoluteEnergy = lufsToEnergy(integratedAbsoluteGateLufs);

    double absoluteSum = 0.0;
    int absoluteCount = 0;

    for (double energy : integratedBlocks)
    {
        if (energy >= absoluteEnergy)
        {
            absoluteSum += energy;
            ++absoluteCount;
        }
    }

    if (absoluteCount == 0 || integratedBlocks.empty())
    {
        publishedIntegrated.store(silenceFloorLufs, std::memory_order_relaxed);
        return;
    }

    const double relativeEnergy = lufsToEnergy(
        energyToLufs(absoluteSum / double(absoluteCount)) - integratedRelativeGateLu);

    double gatedSum = 0.0;
    int gatedCount = 0;

    for (double energy : integratedBlocks)
    {
        if (energy >= absoluteEnergy && energy >= relativeEnergy)
        {
            gatedSum += energy;
            ++gatedCount;
        }
    }

    const float value = gatedCount > 0
        ? static_cast<float>(energyToLufs(gatedSum / double(gatedCount)))
        : silenceFloorLufs;

    publishedIntegrated.store(juce::jmax(silenceFloorLufs, value),
                              std::memory_order_relaxed);
}

void LoudnessMeter::updateLra() noexcept
{
    if (lraCount < 2)
    {
        publishedLra.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const double absoluteEnergy = lufsToEnergy(lraAbsoluteGateLufs);

    double absoluteSum = 0.0;
    int absoluteCount = 0;

    for (int i = 0; i < lraCount; ++i)
    {
        const double energy = lraBlocks[(size_t) i];
        if (energy >= absoluteEnergy)
        {
            absoluteSum += energy;
            ++absoluteCount;
        }
    }

    if (absoluteCount == 0)
    {
        publishedLra.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const double relativeEnergy = lufsToEnergy(
        energyToLufs(absoluteSum / double(absoluteCount)) - lraRelativeGateLu);

    int count = 0;
    for (int i = 0; i < lraCount; ++i)
    {
        const double energy = lraBlocks[(size_t) i];
        if (energy >= absoluteEnergy && energy >= relativeEnergy)
            lraScratch[(size_t) count++] = energyToLufs(energy);
    }

    if (count < 2)
    {
        publishedLra.store(0.0f, std::memory_order_relaxed);
        return;
    }

    std::sort(lraScratch.begin(), lraScratch.begin() + count);

    const auto percentile = [this, count](double p) noexcept
    {
        const double position = p * double(count - 1);
        const int low = static_cast<int>(std::floor(position));
        const int high = static_cast<int>(std::ceil(position));
        const double fraction = position - double(low);
        const double a = lraScratch[(size_t) low];
        const double b = lraScratch[(size_t) high];
        return a + fraction * (b - a);
    };

    const double lra = juce::jmax(0.0, percentile(0.95) - percentile(0.10));
    publishedLra.store(static_cast<float>(lra), std::memory_order_relaxed);
}

double LoudnessMeter::energyToLufs(double energy) noexcept
{
    if (! (energy > 0.0) || ! std::isfinite(energy))
        return -300.0;

    return lufsOffset + 10.0 * std::log10(energy);
}

double LoudnessMeter::lufsToEnergy(double lufs) noexcept
{
    return std::pow(10.0, (lufs - lufsOffset) / 10.0);
}

float LoudnessMeter::amplitudeToDb(double amplitude) noexcept
{
    if (! (amplitude > 1.0e-6) || ! std::isfinite(amplitude))
        return silenceFloorDb;

    return juce::jmax(silenceFloorDb, static_cast<float>(20.0 * std::log10(amplitude)));
}

//==============================================================================
LoudnessMeter::Values LoudnessMeter::getValues() const noexcept
{
    Values v;

    v.momentaryLufs = publishedMomentary.load(std::memory_order_relaxed);
    v.shortTermLufs = publishedShortTerm.load(std::memory_order_relaxed);
    v.integratedLufs = publishedIntegrated.load(std::memory_order_relaxed);
    v.loudnessRangeLu = publishedLra.load(std::memory_order_relaxed);
    v.truePeakDb = publishedTruePeak.load(std::memory_order_relaxed);
    v.samplePeakDb = publishedSamplePeak.load(std::memory_order_relaxed);
    v.rmsDb = publishedRms.load(std::memory_order_relaxed);
    v.correlation = publishedCorrelation.load(std::memory_order_relaxed);
    v.processedSamples = publishedProcessedSamples.load(std::memory_order_relaxed);

    return v;
}

float LoudnessMeter::momentaryLufs() const noexcept
{
    return publishedMomentary.load(std::memory_order_relaxed);
}

float LoudnessMeter::shortTermLufs() const noexcept
{
    return publishedShortTerm.load(std::memory_order_relaxed);
}

float LoudnessMeter::integratedLufs() const noexcept
{
    return publishedIntegrated.load(std::memory_order_relaxed);
}

float LoudnessMeter::loudnessRangeLu() const noexcept
{
    return publishedLra.load(std::memory_order_relaxed);
}

float LoudnessMeter::truePeakDb() const noexcept
{
    return publishedTruePeak.load(std::memory_order_relaxed);
}

float LoudnessMeter::samplePeakDb() const noexcept
{
    return publishedSamplePeak.load(std::memory_order_relaxed);
}

float LoudnessMeter::rmsDb() const noexcept
{
    return publishedRms.load(std::memory_order_relaxed);
}

float LoudnessMeter::correlation() const noexcept
{
    return publishedCorrelation.load(std::memory_order_relaxed);
}

bool LoudnessMeter::isReady() const noexcept
{
    return preparedFlag.load(std::memory_order_acquire);
}
} // namespace aurora
