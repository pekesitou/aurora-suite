#pragma once

#include "Core/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace aurora
{
/**
    Flat, theme-driven LookAndFeel.

    No gradients, shadows, bevels or glossy highlights are used anywhere. Every
    colour comes from the active Theme; the only derived colours are translucent
    versions of the accent used for hover and selection fills.

    The class owns two Typeface pointers loaded once from AuroraAssets:
    InterVariable.ttf for UI text and JetBrainsMono-Variable.ttf for numeric
    readouts. If the embedded font cannot be loaded the theme font family name
    is used as a system fallback, so the editor never fails to open.
*/
class AuroraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit AuroraLookAndFeel(const Theme& initialTheme);
    ~AuroraLookAndFeel() override;

    void applyTheme(const Theme& newTheme);

    [[nodiscard]] const Theme& theme() const noexcept { return currentTheme; }

    [[nodiscard]] juce::Font uiFont(float height, bool bold = false) const;
    [[nodiscard]] juce::Font monoFont(float height, bool bold = false) const;

    // Flat vector controls.
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;

    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;

private:
    void refreshColours();

    Theme currentTheme;
    juce::Typeface::Ptr uiTypeface;
    juce::Typeface::Ptr monoTypeface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuroraLookAndFeel)
};
} // namespace aurora
