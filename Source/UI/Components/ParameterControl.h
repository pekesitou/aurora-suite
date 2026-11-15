#pragma once

#include "Core/ModuleCatalog.h"
#include "Core/ThemeManager.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace aurora
{
/**
    One real, attached control for a catalog parameter.

    Continuous parameters get a vector rotary knob, choices a ComboBox and
    booleans a flat toggle. Every control is connected through an APVTS
    attachment, so host automation, preset changes and UI edits stay in sync in
    both directions. refresh() is called by the parent at UI frame rate to keep
    the numeric readout up to date when the host moves a parameter.
*/
class ParameterControl final : public juce::Component
{
public:
    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const ParamSpec& parameter,
                     const Theme& theme);

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void setSelected(bool shouldBeSelected);
    void setCompact(bool shouldBeCompact);

    [[nodiscard]] const ParamSpec& parameter() const noexcept { return spec; }

private:
    void updateValueText();

    const ParamSpec spec;
    Theme theme;
    bool selected = false;
    bool compact = false;

    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterControl)
};
} // namespace aurora
