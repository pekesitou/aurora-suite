#pragma once

#include "DSP/DspUtils.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace aurora
{
/**
    Real-time safe spectrum analyser with a dedicated worker thread.

    Audio thread:
      push() downmixes to mono, sanitises and writes into a fixed SPSC FIFO
      (juce::AbstractFifo). If the FIFO is full the whole block is dropped and
      counted in droppedSamples. push() never allocates, locks or notifies.

    Worker thread (std::jthread, created by start() and joined by stop()):
      - consumes the FIFO into 4096-sample frames (fftOrder = 12 by default),
      - 75 % overlap (hop = fftSize / 4),
      - Hann or Blackman window (pre-computed in prepare),
      - real-only juce::dsp::FFT, magnitude normalised by the window coherent
        gain so that a full-scale sine reads 0 dB at its bin,
      - configurable exponential averaging and peak hold with dB/s fall,
      - publishes into a lock-free triple buffer.

    UI thread:
      readSpectrum() copies the latest published frame without blocking. It
      returns false when no new frame has been published since the previous
      call, in which case the caller simply keeps drawing its previous data.
      Only one UI reader is supported (single-producer / single-consumer).

    Lifecycle: prepare() stops the worker, reallocates and leaves the analyser
    stopped; call start() afterwards. prepare() must never run on the audio
    thread. reset() is RT-safe and is serviced by the worker.
*/
class SpectrumAnalyzer final
{
public:
    enum class Window : int { hann = 0, blackman = 1 };

    static constexpr int fifoCapacity = 16384;
    static constexpr int slotCount = 3;
    static constexpr float floorDb = -140.0f;

    SpectrumAnalyzer();
    ~SpectrumAnalyzer();

    SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer(SpectrumAnalyzer&&) = delete;
    SpectrumAnalyzer& operator=(SpectrumAnalyzer&&) = delete;

    // Allocates FFT, windows, FIFO and published slots. Stops the worker first.
    void prepare(double newSampleRate, int maxBlockSize, int fftOrder = 12);

    void start();
    void stop();

    // Audio callback. noexcept, allocation-free, lock-free, non-blocking.
    void push(const juce::AudioBuffer<float>& buffer) noexcept;

    // UI thread. Copies the newest published frame. Returns true if it is new.
    [[nodiscard]] bool readSpectrum(std::span<float> destDb) noexcept;

    [[nodiscard]] int getNumBins() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] std::uint64_t getDroppedSamples() const noexcept;

    void setPeakHold(bool enabled) noexcept;
    void setAveraging(float coefficient) noexcept;
    void setWindow(Window window) noexcept;

    // Requests a clear of FIFO/worker state. RT-safe; serviced by the worker
    // on its next wake-up (~5 ms).
    void reset() noexcept;

private:
    void run(std::stop_token token) noexcept;
    void processAvailable() noexcept;
    void processFrame() noexcept;
    void resetWorkerState() noexcept;
    void publishWriteSlot() noexcept;

    static constexpr std::uint32_t newDataFlag = 4u;
    static constexpr std::uint32_t slotMask = 3u;

    double sampleRate = 48000.0;
    int fftOrderValue = 12;
    int fftSize = 4096;
    int hopSize = 1024;
    int bins = 2048;
    double hannSum = 1.0;
    double blackmanSum = 1.0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftData;       // 2 * fftSize, worker only
    std::vector<float> hannWindow;    // fftSize
    std::vector<float> blackmanWindow;// fftSize
    std::vector<float> frameBuffer;   // fftSize, worker only
    std::vector<float> smoothedPower; // bins, worker only
    std::vector<float> peakDb;        // bins, worker only
    int windowFill = 0;

    juce::AbstractFifo fifo { fifoCapacity };
    std::vector<float> fifoStorage;

    std::atomic<bool> preparedFlag { false };
    std::atomic<bool> runningFlag { false };
    std::atomic<bool> resetRequested { false };
    std::atomic<std::uint64_t> droppedSamples { 0 };

    std::atomic<bool> peakHoldEnabled { false };
    std::atomic<float> averagingCoefficient { 0.6f };
    std::atomic<int> selectedWindow { 0 };
    std::atomic<int> numBins { 0 };

    // Triple buffer: producer owns writeSlot, consumer owns frontSlot and the
    // atomic carries the middle slot plus a "new frame" flag.
    std::array<std::vector<float>, slotCount> publishedSlots;
    std::atomic<std::uint32_t> sharedSlot { 0 };
    int writeSlot = 1;
    int frontSlot = 2;

    std::jthread worker;
    std::mutex workerMutex;
    std::condition_variable workerCv;
};
} // namespace aurora
