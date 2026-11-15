#pragma once

#include "Core/ModuleCatalog.h"
#include "Core/ThemeManager.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace aurora
{
class AuroraProcessor;
class ParameterControl;
class EQCurveDisplay;

/**
    Parameter editor for the selected rack slot.

    Controls are generated from ModuleCatalog and attached to the APVTS, so the
    view contains no hard-coded parameter IDs (except the shared "active" flag
    shown in the header). EQ instances also show a live magnitude response
    computed from the current parameter values.
*/
class DetailEditorView final : public juce::Component
{
public:
    DetailEditorView(AuroraProcessor& processor, const Theme& theme);
    ~DetailEditorView() override;

    void setModule(ModuleId id);
    [[nodiscard]] ModuleId getModule() const noexcept { return module; }

    void setTheme(const Theme& newTheme);
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void rebuildControls();
    void layoutGrid();
    void updateTwinState();
    [[nodiscard]] juce::String moduleRankText() const;

    AuroraProcessor& owner;
    Theme theme;
    ModuleId module = ModuleId::utilA;

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ParameterControl> controls;
    std::unique_ptr<EQCurveDisplay> eqCurve;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;

    juce::ToggleButton activeButton { "ACTIVE" };
    juce::TextButton duplicateButton { "DUPLICATE" };
    juce::TextButton defaultsButton { "DEFAULTS" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetailEditorView)
};
} // namespace aurora
