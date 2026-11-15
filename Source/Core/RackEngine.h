#pragma once

#include "Core/ModuleCatalog.h"
#include "DSP/ModuleBase.h"

#include <array>
#include <atomic>
#include <memory>
#include <span>

namespace aurora
{
class RackEngine
{
public:
    explicit RackEngine(juce::AudioProcessorValueTreeState& state);
    ~RackEngine();

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer) noexcept;
    void setTempo(double bpm, bool isPlaying) noexcept;

    [[nodiscard]] int latencySamples() const noexcept { return totalLatency; }
    [[nodiscard]] std::span<const ModuleId> currentOrder() const noexcept;
    [[nodiscard]] ModuleBase* getModule(ModuleId id) noexcept;
    [[nodiscard]] const ModuleBase* getModule(ModuleId id) const noexcept;

    // Message thread only. Returns false if the lock-free command queue is full.
    bool requestOrder(std::span<const ModuleId> newOrder);
    void requestReset() noexcept;

private:
    enum class SlotState : uint8_t { inactive, priming, fadeIn, active, fadeOut };

    struct Slot
    {
        std::unique_ptr<ModuleBase> module;
        ParamRef activeParam;
        juce::AudioBuffer<float> scratch;
        juce::AudioBuffer<float> latencyStorage;
        int latencyWritePos = 0;
        int latency = 0;
        SlotState state = SlotState::inactive;
        int primingRemaining = 0;
        float fadeGain = 0.0f;
        bool lastTarget = false;
    };

    struct OrderCommand
    {
        std::array<ModuleId, kNumModules> order {};
    };

    static constexpr int commandCapacity = 8;

    void applyCommands() noexcept;
    bool popCommand(OrderCommand& out) noexcept;
    void processSlot(Slot& slot, juce::AudioBuffer<float>& buffer, int numChannels) noexcept;
    [[nodiscard]] float delaySample(Slot& slot, int channel, float input) noexcept;
    void applyOrderFade(juce::AudioBuffer<float>& buffer, float from, float to) noexcept;

    juce::AudioProcessorValueTreeState& apvts;
    std::array<Slot, kNumModules> slots;
    std::array<ModuleId, kNumModules> order {};
    std::array<ModuleId, kNumModules> pendingOrder {};
    bool hasPendingOrder = false;
    float orderFade = 1.0f;
    bool orderFadingOut = false;

    std::array<OrderCommand, commandCapacity> commands {};
    std::atomic<uint32_t> commandWrite { 0 };
    std::atomic<uint32_t> commandRead { 0 };

    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> playing { false };
    std::atomic<bool> resetRequested { false };

    double sampleRate = 44100.0;
    int maxBlock = 512;
    int channels = 2;
    int totalLatency = 0;
};
} // namespace aurora
