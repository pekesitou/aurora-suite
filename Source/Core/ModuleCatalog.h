#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <span>
#include <vector>

namespace aurora
{
enum class ModuleType : uint8_t
{
    utility = 0,
    eq,
    compressor,
    multiband,
    deEsser,
    gate,
    transient,
    saturator,
    imager,
    reverb,
    delay,
    mastering
};

enum class ModuleId : uint8_t
{
    utilA = 0,
    utilB,
    eqA,
    eqB,
    compA,
    compB,
    mbA,
    dsA,
    gateA,
    transA,
    satA,
    satB,
    imgA,
    revA,
    dlyA,
    masterA
};

inline constexpr int kNumModules = 16;
inline constexpr int kMaxChannels = 2;
inline constexpr int kMaxBlockSize = 2048;

enum class ParamKind : uint8_t { continuous, boolean, choice };

struct ParamSpec
{
    juce::String id;
    juce::String name;
    ParamKind kind = ParamKind::continuous;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float step = 0.0f;
    float skewCentre = 0.0f; // 0 = linear
    juce::String unit;
    juce::StringArray choices;
    int smoothingMs = 0;

    [[nodiscard]] bool isChoice() const noexcept { return kind == ParamKind::choice; }
    [[nodiscard]] bool isBoolean() const noexcept { return kind == ParamKind::boolean; }
};

struct ModuleSpec
{
    ModuleId id = ModuleId::utilA;
    ModuleType type = ModuleType::utility;
    juce::String name;
    juce::String prefix;
    juce::String shortLabel;
    bool defaultActive = false;
    std::vector<ParamSpec> params;
};

struct Catalog
{
    std::vector<ModuleSpec> modules;
    std::vector<ParamSpec> globals;

    [[nodiscard]] const ModuleSpec& module(ModuleId id) const noexcept
    {
        return modules[static_cast<size_t>(id)];
    }

    [[nodiscard]] const ModuleSpec* findModuleByPrefix(const juce::String& p) const noexcept;

    [[nodiscard]] const ParamSpec* findParam(const juce::String& fullId) const noexcept;
};

const Catalog& catalog();
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

juce::String toString(ModuleId id);
juce::String toString(ModuleType type);
ModuleId moduleIdFromString(const juce::String& text, ModuleId fallback = ModuleId::utilA);
ModuleType moduleTypeFromId(ModuleId id) noexcept;
juce::String instancePrefix(ModuleId id);
std::span<const ModuleId> allModuleIds();

// Default rack order used by the engine and by the state serializer.
std::array<ModuleId, kNumModules> defaultRackOrder();

// ---------- param helper API used by the DSP modules ----------
std::atomic<float>* rawParam(juce::AudioProcessorValueTreeState& state, const juce::String& id);

struct SmoothedParam
{
    std::atomic<float>* source = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
    float lastTarget = 0.0f;

    void bind(juce::AudioProcessorValueTreeState& state, const juce::String& id) noexcept;
    void prepare(double sampleRate, double rampMs, float initialValue) noexcept;
    void updateTarget() noexcept;
    [[nodiscard]] float next() noexcept { return smoother.getNextValue(); }
    [[nodiscard]] float target() const noexcept { return lastTarget; }
    [[nodiscard]] float current() const noexcept { return smoother.getCurrentValue(); }
    void snap(float value) noexcept;
};

struct ParamRef
{
    std::atomic<float>* source = nullptr;

    void bind(juce::AudioProcessorValueTreeState& state, const juce::String& id) noexcept;
    [[nodiscard]] float get() const noexcept
    {
        return source != nullptr ? source->load(std::memory_order_relaxed) : 0.0f;
    }
    [[nodiscard]] bool getBool() const noexcept { return get() > 0.5f; }
    [[nodiscard]] int getChoice() const noexcept { return static_cast<int>(get() + 0.5f); }
};
} // namespace aurora
