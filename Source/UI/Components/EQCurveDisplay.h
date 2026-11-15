#pragma once

#include "Core/ModuleCatalog.h"
#include "Core/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace aurora
{
class AuroraProcessor;

/**
    Real-time magnitude response of one of the parametric EQ instances.

    The curve is recomputed from the live APVTS values using the same RBJ
    coefficient formulas as ParametricEQ, so it always reflects what the DSP is
    doing: band enable flags, filter type, frequency, gain, Q and output trim.
    Band handles are drawn at their current frequency and gain.
*/
class EQCurveDisplay final : public juce::Component,
                            private juce::Timer
{
public:
    EQCurveDisplay(AuroraProcessor& processor, ModuleId moduleId, const Theme& theme);
    ~EQCurveDisplay() override = default;

    void paint(juce::Graphics&) override;
    void setModule(ModuleId newModuleId);
    void setTheme(const Theme& newTheme);
    void refreshNow();

private:
    void timerCallback() override;
    [[nodiscard]] float responseDb(float frequencyHz) const;
    [[nodiscard]] float parameterValue(const juce::String& localId) const;
    [[nodiscard]] float mapFrequencyToX(float frequencyHz) const;
    [[nodiscard]] float mapDbToY(float db) const;

    AuroraProcessor& owner;
    ModuleId module;
    Theme theme;
    std::array<float, 96> curveDb {};
    std::array<float, 8> bandFrequencies {};
    std::array<float, 8> bandGains {};
    std::array<bool, 8> bandActive {};
    bool dirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQCurveDisplay)
};
} // namespace aurora
