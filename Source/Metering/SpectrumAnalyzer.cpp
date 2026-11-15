#include "Metering/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurora
{
namespace
{
    constexpr double peakHoldDecayDbPerSecond = 12.0;
    constexpr int maxFramesPerWake = 4;
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer() = default;

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stop();
}

void SpectrumAnalyzer::prepare(double newSampleRate, int maxBlockSize, int newFftOrder)
{
    stop();

    if (! (newSampleRate >= 8000.0 && newSampleRate <= 768000.0) || maxBlockSize <= 0)
    {
        preparedFlag.store(false, std::memory_order_release);
        return;
    }

    sampleRate = newSampleRate;

    fftOrderValue = juce::jlimit(6, 16, newFftOrder);
    fftSize = 1 << fftOrderValue;
    hopSize = fftSize / 4; // 75 % overlap
    bins = fftSize / 2;    // Nyquist bin omitted

    fft = std::make_unique<juce::dsp::FFT>(fftOrderValue);

    fftData.assign(static_cast<size_t>(2 * fftSize), 0.0f);
    frameBuffer.assign(static_cast<size_t>(fftSize), 0.0f);

    hannWindow.assign(static_cast<size_t>(fftSize), 0.0f);
    blackmanWindow.assign(static_cast<size_t>(fftSize), 0.0f);

    hannSum = 0.0;
    blackmanSum = 0.0;

    for (int i = 0; i < fftSize; ++i)
    {
        const double phase = dsp::twoPi * double(i) / double(fftSize - 1);

        const double hann = 0.5 - 0.5 * std::cos(phase);
        const double blackman = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);

        hannWindow[(size_t) i] = static_cast<float>(hann);
        blackmanWindow[(size_t) i] = static_cast<float>(blackman);

        hannSum += hann;
        blackmanSum += blackman;
    }

    smoothedPower.assign(static_cast<size_t>(bins), 0.0f);
    peakDb.assign(static_cast<size_t>(bins), floorDb);

    fifoStorage.assign(static_cast<size_t>(fifoCapacity), 0.0f);
    fifo.reset();

    for (auto& slot : publishedSlots)
    {
        slot.assign(static_cast<size_t>(bins), floorDb);
    }

    numBins.store(bins, std::memory_order_relaxed);

    sharedSlot.store(0u, std::memory_order_relaxed);
    writeSlot = 1;
    frontSlot = 2;

    windowFill = 0;
    droppedSamples.store(0, std::memory_order_relaxed);
    resetRequested.store(false, std::memory_order_relaxed);

    preparedFlag.store(true, std::memory_order_release);
}

void SpectrumAnalyzer::start()
{
    if (! preparedFlag.load(std::memory_order_acquire))
        return;

    if (worker.joinable())
        return;

    worker = std::jthread([this](std::stop_token token) { run(token); });
    runningFlag.store(true, std::memory_order_release);
}

void SpectrumAnalyzer::stop()
{
    runningFlag.store(false, std::memory_order_release);

    if (worker.joinable())
    {
        worker.request_stop();
        workerCv.notify_all();
        worker.join();
    }
}

void SpectrumAnalyzer::push(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (! preparedFlag.load(std::memory_order_acquire))
        return;

    const int numSamples = buffer.getNumSamples();
    const int channelCount = juce::jmin(buffer.getNumChannels(), 8);

    if (numSamples <= 0 || channelCount <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 + size2 < numSamples)
    {
        droppedSamples.fetch_add(static_cast<std::uint64_t>(numSamples),
                                 std::memory_order_relaxed);
        return;
    }

    std::array<const float*, 8> channels {};
    for (int ch = 0; ch < channelCount; ++ch)
        channels[(size_t) ch] = buffer.getReadPointer(ch);

    const float scale = 1.0f / static_cast<float>(channelCount);

    for (int i = 0; i < size1; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < channelCount; ++ch)
            sum += channels[(size_t) ch][i];

        fifoStorage[(size_t) (start1 + i)] = dsp::sanitize(sum * scale);
    }

    for (int i = 0; i < size2; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < channelCount; ++ch)
            sum += channels[(size_t) ch][size1 + i];

        fifoStorage[(size_t) (start2 + i)] = dsp::sanitize(sum * scale);
    }

    fifo.finishedWrite(size1 + size2);
}

bool SpectrumAnalyzer::readSpectrum(std::span<float> destDb) noexcept
{
    if (destDb.empty() || ! preparedFlag.load(std::memory_order_acquire))
        return false;

    const std::uint32_t current = sharedSlot.load(std::memory_order_acquire);

    if ((current & newDataFlag) == 0u)
        return false;

    const std::uint32_t old = sharedSlot.exchange(
        static_cast<std::uint32_t>(frontSlot), std::memory_order_acq_rel);

    frontSlot = static_cast<int>(old & slotMask);

    const auto& slot = publishedSlots[(size_t) frontSlot];
    const auto count = std::min(destDb.size(), slot.size());
    std::copy_n(slot.begin(), count, destDb.begin());

    return true;
}

int SpectrumAnalyzer::getNumBins() const noexcept
{
    return numBins.load(std::memory_order_relaxed);
}

bool SpectrumAnalyzer::isRunning() const noexcept
{
    return runningFlag.load(std::memory_order_acquire);
}

bool SpectrumAnalyzer::isReady() const noexcept
{
    return preparedFlag.load(std::memory_order_acquire);
}

std::uint64_t SpectrumAnalyzer::getDroppedSamples() const noexcept
{
    return droppedSamples.load(std::memory_order_relaxed);
}

void SpectrumAnalyzer::setPeakHold(bool enabled) noexcept
{
    peakHoldEnabled.store(enabled, std::memory_order_relaxed);
}

void SpectrumAnalyzer::setAveraging(float coefficient) noexcept
{
    if (! std::isfinite(coefficient))
        coefficient = 0.6f;

    averagingCoefficient.store(juce::jlimit(0.0f, 0.99f, coefficient),
                               std::memory_order_relaxed);
}

void SpectrumAnalyzer::setWindow(Window window) noexcept
{
    const int value = (window == Window::blackman) ? 1 : 0;
    selectedWindow.store(value, std::memory_order_relaxed);
}

void SpectrumAnalyzer::reset() noexcept
{
    resetRequested.store(true, std::memory_order_release);
    droppedSamples.store(0, std::memory_order_relaxed);
}

void SpectrumAnalyzer::run(std::stop_token token) noexcept
{
    try
    {
        std::unique_lock lock { workerMutex };

        while (! token.stop_requested())
        {
            workerCv.wait_for(lock, std::chrono::milliseconds(5));

            if (token.stop_requested())
                break;

            lock.unlock();
            processAvailable();
            lock.lock();
        }
    }
    catch (...)
    {
        // A worker must never take the plugin down. The audio thread keeps
        // running; the FIFO simply reports drops when it fills up.
    }

    runningFlag.store(false, std::memory_order_release);
}

void SpectrumAnalyzer::processAvailable() noexcept
{
    if (resetRequested.exchange(false, std::memory_order_acq_rel))
        resetWorkerState();

    int frames = 0;

    while (frames < maxFramesPerWake)
    {
        if (windowFill >= fftSize)
        {
            processFrame();

            const int keep = fftSize - hopSize;
            std::memmove(frameBuffer.data(),
                         frameBuffer.data() + hopSize,
                         static_cast<size_t>(keep) * sizeof(float));
            windowFill = keep;
            ++frames;
            continue;
        }

        const int available = fifo.getNumReady();

        if (available <= 0)
            break;

        const int wanted = fftSize - windowFill;
        const int toRead = juce::jmin(wanted, available);

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(toRead, start1, size1, start2, size2);

        std::copy_n(fifoStorage.data() + start1, size1,
                    frameBuffer.data() + windowFill);
        std::copy_n(fifoStorage.data() + start2, size2,
                    frameBuffer.data() + windowFill + size1);

        fifo.finishedRead(size1 + size2);
        windowFill += size1 + size2;
    }
}

void SpectrumAnalyzer::processFrame() noexcept
{
    const bool useBlackman = selectedWindow.load(std::memory_order_relaxed) != 0;

    const float* window = (useBlackman ? blackmanWindow : hannWindow).data();
    const double windowSum = useBlackman ? blackmanSum : hannSum;

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = frameBuffer[(size_t) i] * window[i];

    std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);

    fft->performRealOnlyForwardTransform(fftData.data(), true);

    const float averaging = juce::jlimit(
        0.0f, 0.99f, averagingCoefficient.load(std::memory_order_relaxed));
    const float oneMinusAveraging = 1.0f - averaging;

    const bool hold = peakHoldEnabled.load(std::memory_order_relaxed);
    const float decayDb = static_cast<float>(
        peakHoldDecayDbPerSecond * double(hopSize) / sampleRate);

    auto& target = publishedSlots[(size_t) writeSlot];

    for (int bin = 0; bin < bins; ++bin)
    {
        const double re = double(fftData[(size_t) (2 * bin)]);
        const double im = double(fftData[(size_t) (2 * bin + 1)]);

        // Real FFT: the DC bin is not mirrored, all others are.
        const double binScale = windowSum > 1.0e-12
            ? (bin == 0 ? 1.0 / windowSum : 2.0 / windowSum)
            : (bin == 0 ? 1.0 / double(fftSize) : 2.0 / double(fftSize));

        const double amplitude = std::sqrt(re * re + im * im) * binScale;
        const float power = static_cast<float>(amplitude * amplitude);

        auto& smoothed = smoothedPower[(size_t) bin];
        smoothed = averaging * smoothed + oneMinusAveraging * power;

        float db = 10.0f * std::log10(juce::jmax(smoothed, 1.0e-20f));
        db = juce::jmax(floorDb, db);

        if (hold)
        {
            auto& peak = peakDb[(size_t) bin];
            peak = juce::jmax(db, peak - decayDb);
            target[(size_t) bin] = juce::jmax(db, peak);
        }
        else
        {
            peakDb[(size_t) bin] = db;
            target[(size_t) bin] = db;
        }
    }

    publishWriteSlot();
}

void SpectrumAnalyzer::resetWorkerState() noexcept
{
    windowFill = 0;

    std::fill(smoothedPower.begin(), smoothedPower.end(), 0.0f);
    std::fill(peakDb.begin(), peakDb.end(), floorDb);

    // Discard everything currently queued so the new frame starts clean.
    const int ready = fifo.getNumReady();

    if (ready > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(ready, start1, size1, start2, size2);
        fifo.finishedRead(size1 + size2);
    }

    auto& target = publishedSlots[(size_t) writeSlot];
    std::fill(target.begin(), target.end(), floorDb);
    publishWriteSlot();
}

void SpectrumAnalyzer::publishWriteSlot() noexcept
{
    const std::uint32_t old = sharedSlot.exchange(
        static_cast<std::uint32_t>(writeSlot) | newDataFlag,
        std::memory_order_acq_rel);

    writeSlot = static_cast<int>(old & slotMask);
}
} // namespace aurora
