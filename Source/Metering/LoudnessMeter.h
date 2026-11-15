#pragma once

#include "DSP/DspUtils.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace aurora
{
/**
    EBU R128 / ITU-R BS.1770-4 loudness meter.

    Runs entirely on the audio thread. All storage is allocated in prepare();
    process() performs no allocation, takes no locks and is noexcept.

    Measured values (published as independent atomics, read with getValues()):
      - K-weighting: ITU-R BS.1770 pre-filter (high shelf) + RLB high-pass.
        Coefficients are recomputed in prepare() for any sample rate using the
        libebur128 formulas (f0 = 1681.974450955533 Hz / G = 3.999843853973347 dB /
        Q = 0.7071752369554196, and f0 = 38.13547087602444 Hz / Q = 0.5003270373238773).
      - Momentary: 400 ms sliding window, 100 ms step (75 % overlap).
      - Short-term: 3 s sliding window, 100 ms step.
      - Integrated: 400 ms blocks, absolute gate -70 LUFS, relative gate -10 LU.
      - LRA: 3 s blocks with 1 s step, absolute gate -70 LUFS, relative gate -20 LU,
        10th / 95th percentiles per EBU Tech 3342. (A "-20 LU absolute" gate has no
        physical meaning: the absolute gate is always referenced to LUFS.)
      - True peak: 4x oversampling with a dedicated causal polyphase FIR,
        128 taps (32 per phase), Blackman-windowed sinc, cutoff at 0.125 of the
        4x rate, each phase normalised to unity DC gain. Group delay is
        (numTaps - 1) / 8 input samples. juce::dsp::Oversampling is deliberately
        not used: this FIR is allocation-free, has no block-size-dependent state
        and cannot fail at runtime.
      - Sample peak, 400 ms RMS and L/R correlation (exponential mean, tau 300 ms).

    Gating history is bounded (integrated: 1 hour of 100 ms blocks, LRA: 1 hour of
    1 s blocks). Beyond that the oldest LRA blocks are recycled and integrated
    blocks stop accumulating; this is a hard real-time bound, documented on purpose.

    Channel weights follow BS.1770 for stereo (1.0 / 1.0). Surround layouts are
    out of scope for the rack (see docs/01_ARCHITECTURE.md); every channel present
    in the buffer is weighted 1.0.
*/
class LoudnessMeter final
{
public:
    static constexpr int maxChannels = 8;
    static constexpr float silenceFloorLufs = -120.0f;
    static constexpr float silenceFloorDb = -120.0f;

    struct Values
    {
        float momentaryLufs = silenceFloorLufs;
        float shortTermLufs = silenceFloorLufs;
        float integratedLufs = silenceFloorLufs;
        float loudnessRangeLu = 0.0f;
        float truePeakDb = silenceFloorDb;
        float samplePeakDb = silenceFloorDb;
        float rmsDb = silenceFloorDb;
        float correlation = 1.0f;
        std::uint64_t processedSamples = 0;
    };

    LoudnessMeter() = default;
    ~LoudnessMeter() = default;

    LoudnessMeter(const LoudnessMeter&) = delete;
    LoudnessMeter& operator=(const LoudnessMeter&) = delete;
    LoudnessMeter(LoudnessMeter&&) = delete;
    LoudnessMeter& operator=(LoudnessMeter&&) = delete;

    // Allocates history. Call from prepareToPlay (message / host thread), never
    // concurrently with process(). Not noexcept by design.
    void prepare(double newSampleRate, int maxBlockSize, int numChannels);

    // Audio callback. noexcept, allocation-free, lock-free.
    void process(const juce::AudioBuffer<float>& buffer) noexcept;

    // Clears filters, gating history and published values. noexcept, RT-safe.
    void reset() noexcept;

    // Requests a true-peak / sample-peak hold reset. Handled at the next process
    // call so that it is race-free with respect to the audio thread.
    void resetPeakHold() noexcept;

    [[nodiscard]] Values getValues() const noexcept;

    [[nodiscard]] float momentaryLufs() const noexcept;
    [[nodiscard]] float shortTermLufs() const noexcept;
    [[nodiscard]] float integratedLufs() const noexcept;
    [[nodiscard]] float loudnessRangeLu() const noexcept;
    [[nodiscard]] float truePeakDb() const noexcept;
    [[nodiscard]] float samplePeakDb() const noexcept;
    [[nodiscard]] float rmsDb() const noexcept;
    [[nodiscard]] float correlation() const noexcept;

    [[nodiscard]] bool isReady() const noexcept;

private:
    // 4x causal polyphase interpolator used only for true-peak detection.
    class TruePeakOversampler
    {
    public:
        static constexpr int factor = 4;
        static constexpr int tapsPerPhase = 32;
        static constexpr int numTaps = factor * tapsPerPhase;
        static constexpr int historySize = 32; // power of two, >= tapsPerPhase

        void prepare() noexcept;
        void reset() noexcept;
        [[nodiscard]] float processSample(int channel, float x) noexcept;
        void advance() noexcept;

    private:
        std::array<float, numTaps> coeffs {};
        std::array<std::array<float, historySize>, maxChannels> history {};
        int writePos = 0;
    };

    struct BiquadChain
    {
        dsp::Biquad shelf;
        dsp::Biquad highPass;
    };

    static constexpr int momentarySubBlocks = 4;   // 400 ms
    static constexpr int shortTermSubBlocks = 30;  // 3 s
    static constexpr int integratedCapacity = 36000; // 1 h at 10 blocks/s
    static constexpr int lraCapacity = 3600;         // 1 h at 1 block/s

    void computeKWeightingCoefficients() noexcept;
    void finishSubBlock() noexcept;
    void updateIntegrated() noexcept;
    void updateLra() noexcept;

    [[nodiscard]] static double energyToLufs(double energy) noexcept;
    [[nodiscard]] static double lufsToEnergy(double lufs) noexcept;
    [[nodiscard]] static float amplitudeToDb(double amplitude) noexcept;

    // Configuration (written only in prepare()).
    double sampleRate = 48000.0;
    int numChannels = 2;
    std::int64_t samplesPerSubBlock = 4800;

    dsp::BiquadCoeffs shelfCoeffs {};
    dsp::BiquadCoeffs highPassCoeffs {};
    std::array<BiquadChain, maxChannels> filters {};
    TruePeakOversampler truePeak;

    // Per-sub-block accumulators.
    std::int64_t subBlockPosition = 0;
    double subBlockWeightedEnergy = 0.0;
    double subBlockRmsEnergy = 0.0;
    double subBlockL = 0.0, subBlockR = 0.0;
    double subBlockLL = 0.0, subBlockRR = 0.0, subBlockLR = 0.0;
    std::uint64_t subBlockCounter = 0;

    // Sliding windows.
    std::array<double, momentarySubBlocks> momentaryRing {};
    int momentaryIndex = 0;
    int momentaryCount = 0;

    std::array<double, shortTermSubBlocks> shortTermRing {};
    int shortTermIndex = 0;
    int shortTermCount = 0;

    std::array<double, momentarySubBlocks> rmsRing {};
    int rmsIndex = 0;
    int rmsCount = 0;

    // Gating history.
    std::vector<double> integratedBlocks;
    std::array<double, lraCapacity> lraBlocks {};
    std::array<double, lraCapacity> lraScratch {};
    int lraCount = 0;
    int lraWriteIndex = 0;

    // Peaks.
    double maxTruePeak = 0.0;
    double maxSamplePeak = 0.0;
    std::atomic<bool> peakResetRequested { false };

    // Correlation exponential means (~300 ms).
    double correlationCoeff = 0.0;
    double corrMeanL = 0.0, corrMeanR = 0.0;
    double corrMeanLL = 0.0, corrMeanRR = 0.0, corrMeanLR = 0.0;

    // Published values.
    std::atomic<float> publishedMomentary { silenceFloorLufs };
    std::atomic<float> publishedShortTerm { silenceFloorLufs };
    std::atomic<float> publishedIntegrated { silenceFloorLufs };
    std::atomic<float> publishedLra { 0.0f };
    std::atomic<float> publishedTruePeak { silenceFloorDb };
    std::atomic<float> publishedSamplePeak { silenceFloorDb };
    std::atomic<float> publishedRms { silenceFloorDb };
    std::atomic<float> publishedCorrelation { 1.0f };
    std::atomic<std::uint64_t> publishedProcessedSamples { 0 };
    std::atomic<bool> preparedFlag { false };
};
} // namespace aurora
