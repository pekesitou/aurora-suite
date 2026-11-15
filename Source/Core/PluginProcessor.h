#pragma once

#include "Core/ModuleCatalog.h"
#include "Core/RackEngine.h"
#include "Metering/LoudnessMeter.h"
#include "Metering/PhaseScope.h"
#include "Metering/SpectrumAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <optional>

namespace aurora
{
class ThemeManager;
class ARIAAnalyzer;
class ARIARefEngine;

class AuroraProcessor final : public juce::AudioProcessor
{
public:
    AuroraProcessor();
    ~AuroraProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Aurora Engine"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    RackEngine& getRack() noexcept { return rack; }
    ThemeManager& getThemeManager() noexcept;

    void setThemeId(const juce::String& id);
    [[nodiscard]] juce::String getThemeId() const;

    // Message thread only.
    [[nodiscard]] std::array<ModuleId, kNumModules> getRackOrder() const;
    void setRackOrder(std::span<const ModuleId> newOrder);
    void setModuleActive(ModuleId id, bool shouldBeActive);
    [[nodiscard]] bool isModuleActive(ModuleId id) const;
    [[nodiscard]] std::optional<ModuleId> findTwinModule(ModuleId id) const;

    /** Copies every parameter of source into the twin instance, activates it and
        inserts it directly after source. Returns the twin id, if one exists. */
    std::optional<ModuleId> duplicateModule(ModuleId source);

    /** Restores the catalog defaults of every parameter of one slot. */
    void resetModuleToDefaults(ModuleId id);

    [[nodiscard]] int getPluginLatencySamples() const noexcept { return reportedLatency; }
    [[nodiscard]] float getInputPeakDb() const noexcept { return inputPeak.load(std::memory_order_relaxed); }
    [[nodiscard]] float getOutputPeakDb() const noexcept { return outputPeak.load(std::memory_order_relaxed); }
    [[nodiscard]] juce::String getLastStateError() const;

    // ARIA hooks (implemented in Phase 6; wired here so the editor can stay decoupled).
    void requestAriaAnalysis();
    [[nodiscard]] bool isAriaAnalysisRunning() const noexcept;

private:
    void processChunk(juce::AudioBuffer<float>& buffer) noexcept;
    void updateRackOrderProperty();
    void loadRackOrderFromState(const juce::ValueTree& state);

    juce::AudioProcessorValueTreeState apvts;
    RackEngine rack;
    std::unique_ptr<ThemeManager> themeManager;
    std::unique_ptr<ARIAAnalyzer> aria;

    SmoothedParam inputTrim, outputTrim;
    std::atomic<float> inputPeak { -120.0f };
    std::atomic<float> outputPeak { -120.0f };

    std::array<ModuleId, kNumModules> uiOrder {};
    int reportedLatency = 0;
    int maxBlockPrepared = 512;
    int numChannelsPrepared = 2;
    double currentSampleRate = 44100.0;
    juce::String themeId { "dark-matter-flat" };
    juce::String lastStateError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuroraProcessor)
};
} // namespace aurora
