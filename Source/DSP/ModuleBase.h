#pragma once

#include "Core/ModuleCatalog.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace aurora
{
class ModuleBase
{
public:
    virtual ~ModuleBase() = default;

    // Called from prepareToPlay / state restore. May allocate; never from the audio callback.
    virtual void prepare(double newSampleRate, int maxBlockSize, int numChannels) = 0;

    // Audio callback. Must be noexcept, allocation-free and lock-free.
    virtual void process(juce::AudioBuffer<float>& buffer) noexcept = 0;

    virtual void reset() noexcept = 0;

    // Called once per block by RackEngine when host transport information is available.
    virtual void setTempo(double bpm, bool isPlaying) noexcept
    {
        juce::ignoreUnused(bpm, isPlaying);
    }

    [[nodiscard]] virtual int latencySamples() const noexcept = 0;

    [[nodiscard]] virtual float gainReductionDb() const noexcept { return 0.0f; }

    [[nodiscard]] virtual float outputLevelDb() const noexcept { return -120.0f; }

    [[nodiscard]] ModuleId moduleId() const noexcept { return id; }
    [[nodiscard]] ModuleType moduleType() const noexcept { return type; }

protected:
    explicit ModuleBase(ModuleId moduleId) : id(moduleId), type(moduleTypeFromId(moduleId)) {}

    ModuleId id;
    ModuleType type;
};
} // namespace aurora
