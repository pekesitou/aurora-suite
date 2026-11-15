#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace aurora
{
class AuroraProcessor;

struct AriaSuggestion
{
    juce::String parameterId;
    float targetValue = 0.0f;
    juce::String label;
    bool isDiscrete = false;
};

struct ARIAReport
{
    bool valid = false;
    int framesAnalysed = 0;
    float integratedLufs = -70.0f;
    float shortTermLufs = -70.0f;
    float lra = 0.0f;
    float truePeakDb = -120.0f;
    float samplePeakDb = -120.0f;
    float rmsDb = -120.0f;
    float crestDb = 0.0f;
    float spectralCentroidHz = 0.0f;
    float spectralTiltDbPerOct = 0.0f;
    float lowMidDb = -120.0f;
    float sibilanceDb = -120.0f;
    float correlation = 0.0f;
    std::array<float, 31> bandsDb {};
    juce::String diagnosis;
    juce::StringArray details;
    std::vector<AriaSuggestion> suggestions;
};

class ARIAAnalyzer
{
public:
    explicit ARIAAnalyzer(AuroraProcessor& processor);
    ~ARIAAnalyzer();

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void stop();
    void push(const juce::AudioBuffer<float>& buffer) noexcept;
    void requestAnalysis();
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool hasReport() const noexcept;
    [[nodiscard]] ARIAReport getReport() const;

private:
    void run(std::stop_token token);
    void analyseSnapshot();

    AuroraProcessor& owner;
    double sampleRate = 44100.0;
    int channels = 2;
    int capacityFrames = 0;

    std::array<juce::AudioBuffer<float>, 2> capture;
    std::atomic<int> activeBuffer { 0 };
    std::atomic<int> frozenBuffer { 0 };
    std::atomic<int> frozenLength { 0 };
    std::atomic<int> writePosition { 0 };
    std::atomic<bool> freezeRequested { false };
    std::atomic<bool> snapshotReady { false };
    std::atomic<bool> running { false };
    std::atomic<bool> hasNewReport { false };

    std::jthread worker;
    mutable std::mutex reportMutex;
    ARIAReport report;
};
} // namespace aurora
