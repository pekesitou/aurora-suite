#include "AuroraLookAndFeel.h"

#include <BinaryData.h>

namespace aurora
{
namespace
{
juce::Typeface::Ptr loadTypeface(const char* resourceName)
{
    int size = 0;
    if (const auto* data = AuroraAssets::getNamedResource(resourceName, size))
        if (data != nullptr && size > 0)
            return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
    return {};
}

juce::Colour mixColour(juce::Colour a, juce::Colour b, float amount) noexcept
{
    return a.interpolatedWith(b, juce::jlimit(0.0f, 1.0f, amount));
}
} // namespace

AuroraLookAndFeel::AuroraLookAndFeel(const Theme& initialTheme)
    : currentTheme(initialTheme)
{
    uiTypeface = loadTypeface("InterVariable_ttf");
    monoTypeface = loadTypeface("JetBrainsMono_Variable_ttf");

    juce::LookAndFeel_V4::setDefaultSansSerifTypeface(uiTypeface);

    refreshColours();
}

AuroraLookAndFeel::~AuroraLookAndFeel() = default;

void AuroraLookAndFeel::applyTheme(const Theme& newTheme)
{
    currentTheme = newTheme;
    refreshColours();
}

juce::Font AuroraLookAndFeel::uiFont(float height, bool bold) const
{
    if (uiTypeface != nullptr)
    {
        auto options = juce::FontOptions(uiTypeface).withHeight(height);
        if (bold)
            options = options.withStyle("Bold");
        return juce::Font(options);
    }

    return juce::Font(juce::FontOptions(currentTheme.uiFamily, height,
                                        bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font AuroraLookAndFeel::monoFont(float height, bool bold) const
{
    if (monoTypeface != nullptr)
    {
        auto options = juce::FontOptions(monoTypeface).withHeight(height);
        if (bold)
            options = options.withStyle("Bold");
        return juce::Font(options);
    }

    return juce::Font(juce::FontOptions(currentTheme.monoFamily, height,
                                        bold ? juce::Font::bold : juce::Font::plain));
}

void AuroraLookAndFeel::refreshColours()
{
    const auto& t = currentTheme;

    setColour(juce::ResizableWindow::backgroundColourId, t.background);
    setColour(juce::DocumentWindow::textColourId, t.textPrimary);

    setColour(juce::Label::textColourId, t.textPrimary);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, t.surfaceAlt);
    setColour(juce::TextButton::buttonOnColourId, t.selection);
    setColour(juce::TextButton::textColourOffId, t.textSecondary);
    setColour(juce::TextButton::textColourOnId, t.textPrimary);

    setColour(juce::ToggleButton::textColourId, t.textPrimary);
    setColour(juce::ToggleButton::tickColourId, t.accent);
    setColour(juce::ToggleButton::tickDisabledColourId, t.textDisabled);

    setColour(juce::ComboBox::backgroundColourId, t.surfaceAlt);
    setColour(juce::ComboBox::textColourId, t.textPrimary);
    setColour(juce::ComboBox::outlineColourId, t.border);
    setColour(juce::ComboBox::arrowColourId, t.textSecondary);
    setColour(juce::ComboBox::buttonColourId, t.surfaceAlt);

    setColour(juce::PopupMenu::backgroundColourId, t.surface);
    setColour(juce::PopupMenu::textColourId, t.textPrimary);
    setColour(juce::PopupMenu::headerTextColourId, t.textSecondary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.selection);
    setColour(juce::PopupMenu::highlightedTextColourId, t.textPrimary);

    setColour(juce::Slider::backgroundColourId, t.surfaceAlt);
    setColour(juce::Slider::trackColourId, t.accent);
    setColour(juce::Slider::thumbColourId, t.knobPrimary);
    setColour(juce::Slider::rotarySliderFillColourId, t.accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, t.knobRing);
    setColour(juce::Slider::textBoxTextColourId, t.textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, t.selection);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::TextEditor::backgroundColourId, t.background);
    setColour(juce::TextEditor::textColourId, t.textPrimary);
    setColour(juce::TextEditor::outlineColourId, t.border);
    setColour(juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour(juce::TextEditor::highlightColourId, t.selection);
    setColour(juce::TextEditor::highlightedTextColourId, t.textPrimary);
    setColour(juce::CaretComponent::caretColourId, t.accent);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, t.borderStrong);

    setColour(juce::TooltipWindow::backgroundColourId, t.surface);
    setColour(juce::TooltipWindow::textColourId, t.textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, t.border);

    setColour(juce::ListBox::backgroundColourId, t.background);
    setColour(juce::ListBox::textColourId, t.textPrimary);
    setColour(juce::ListBox::outlineColourId, t.border);

    setColour(juce::TabbedComponent::backgroundColourId, t.background);
    setColour(juce::TabbedComponent::outlineColourId, t.border);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, t.border);
    setColour(juce::TabbedButtonBar::tabTextColourId, t.textSecondary);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, t.accent);
    setColour(juce::TabbedButtonBar::frontTextColourId, t.textPrimary);

    setColour(juce::AlertWindow::backgroundColourId, t.surface);
    setColour(juce::AlertWindow::textColourId, t.textPrimary);
    setColour(juce::AlertWindow::outlineColourId, t.border);
}

void AuroraLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const auto& t = currentTheme;
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getCentreY();
    const float angle = rotaryStartAngle + juce::jlimit(0.0f, 1.0f, sliderPos)
                                          * (rotaryEndAngle - rotaryStartAngle);
    const float ringRadius = radius - 2.0f;
    const float ringThickness = juce::jmax(2.0f, radius * 0.12f);

    juce::Path track;
    track.addCentredArc(centreX, centreY, ringRadius, ringRadius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(t.knobRing);
    g.strokePath(track, juce::PathStrokeType(ringThickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc(centreX, centreY, ringRadius, ringRadius, 0.0f,
                            rotaryStartAngle, angle, true);
        g.setColour(slider.isEnabled() ? t.accent : t.textDisabled);
        g.strokePath(value, juce::PathStrokeType(ringThickness, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const float bodyRadius = ringRadius - ringThickness - 3.0f;
    g.setColour(slider.isMouseOverOrDragging() ? mixColour(t.knobBody, t.surfaceAlt, 0.5f) : t.knobBody);
    g.fillEllipse(centreX - bodyRadius, centreY - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour(t.border);
    g.drawEllipse(centreX - bodyRadius, centreY - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    const float pointerInner = bodyRadius * 0.30f;
    const float pointerOuter = bodyRadius * 0.82f;
    juce::Path pointer;
    pointer.startNewSubPath(centreX + std::sin(angle) * pointerInner,
                            centreY - std::cos(angle) * pointerInner);
    pointer.lineTo(centreX + std::sin(angle) * pointerOuter,
                   centreY - std::cos(angle) * pointerOuter);
    g.setColour(slider.isEnabled() ? t.knobPrimary : t.textDisabled);
    g.strokePath(pointer, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    g.setColour(t.accent);
    g.fillEllipse(centreX - 1.5f, centreY - 1.5f, 3.0f, 3.0f);
}

void AuroraLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos);
    const auto& t = currentTheme;

    if (style == juce::Slider::LinearVertical)
    {
        const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        juce::Rectangle<float> track(cx - 2.0f, static_cast<float>(y), 4.0f,
                                     static_cast<float>(height));
        g.setColour(t.surfaceAlt);
        g.fillRect(track);
        g.setColour(t.accent);
        g.fillRect(track.withTop(sliderPos));
        g.setColour(slider.isMouseOverOrDragging() ? t.accent : t.knobPrimary);
        g.fillRect(juce::Rectangle<float>(track.getX() - 4.0f, sliderPos - 2.0f,
                                          track.getWidth() + 8.0f, 4.0f));
        return;
    }

    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    juce::Rectangle<float> track(static_cast<float>(x), cy - 2.0f, static_cast<float>(width), 4.0f);
    g.setColour(t.surfaceAlt);
    g.fillRect(track);
    g.setColour(t.accent);
    g.fillRect(track.withRight(sliderPos));
    g.setColour(slider.isMouseOverOrDragging() ? t.accent : t.knobPrimary);
    g.fillRect(juce::Rectangle<float>(sliderPos - 2.0f, cy - 6.0f, 4.0f, 12.0f));
}

void AuroraLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool highlighted, bool down)
{
    juce::ignoreUnused(backgroundColour);
    const auto& t = currentTheme;
    const auto bounds = button.getLocalBounds().toFloat();
    const auto radius = static_cast<float>(t.cornerRadius);

    auto fill = button.getToggleState() ? t.selection : t.surfaceAlt;
    if (down)
        fill = mixColour(fill, t.background, 0.28f);
    else if (highlighted)
        fill = mixColour(fill, t.accent, 0.10f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(button.getToggleState() ? t.accent : t.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

void AuroraLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                       bool, bool)
{
    const auto& t = currentTheme;
    g.setFont(uiFont(juce::jmin(13.0f, static_cast<float>(button.getHeight()) * 0.46f), true));
    g.setColour(button.isEnabled()
                    ? (button.getToggleState() ? t.textPrimary : t.textSecondary)
                    : t.textDisabled);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(2),
                     juce::Justification::centred, 1, 0.85f);
}

void AuroraLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool highlighted, bool)
{
    const auto& t = currentTheme;
    auto bounds = button.getLocalBounds();
    const int box = juce::jmin(14, bounds.getHeight() - 2);
    juce::Rectangle<int> led(bounds.getX() + 2, bounds.getCentreY() - box / 2, box, box);

    g.setColour(button.getToggleState() ? t.accent : t.surfaceAlt);
    g.fillRect(led);
    g.setColour(button.getToggleState() ? t.accent : t.border);
    g.drawRect(led, 1);

    g.setColour(button.isEnabled()
                    ? (highlighted ? t.textPrimary : t.textSecondary)
                    : t.textDisabled);
    g.setFont(uiFont(static_cast<float>(juce::jmax(10, box)), false));
    g.drawFittedText(button.getButtonText(), bounds.withTrimmedLeft(box + 8),
                     juce::Justification::centredLeft, 1, 0.9f);
}

void AuroraLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box)
{
    const auto& t = currentTheme;
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    const auto radius = static_cast<float>(t.cornerRadius);

    g.setColour(t.surfaceAlt);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(box.hasKeyboardFocus(false) ? t.accent : t.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    const float cx = static_cast<float>(width) - 12.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.0f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(t.textSecondary);
    g.strokePath(arrow, juce::PathStrokeType(1.5f));
}

void AuroraLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 0, box.getWidth() - 26, box.getHeight());
    label.setFont(uiFont(juce::jmin(12.0f, static_cast<float>(box.getHeight()) * 0.48f)));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Font AuroraLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return uiFont(juce::jmin(12.0f, static_cast<float>(box.getHeight()) * 0.48f));
}

juce::Font AuroraLookAndFeel::getPopupMenuFont()
{
    return uiFont(12.0f);
}

juce::Font AuroraLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return uiFont(juce::jmin(13.0f, static_cast<float>(buttonHeight) * 0.46f), true);
}

juce::Font AuroraLookAndFeel::getLabelFont(juce::Label& label)
{
    return uiFont(juce::jmin(13.0f, static_cast<float>(label.getHeight()) * 0.66f));
}

void AuroraLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto& t = currentTheme;
    g.setColour(t.surface);
    g.fillRect(0, 0, width, height);
    g.setColour(t.border);
    g.drawRect(0, 0, width, height, 1);
}

void AuroraLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool hasSubMenu,
                                          const juce::String& text,
                                          const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon,
                                          const juce::Colour* textColour)
{
    juce::ignoreUnused(icon, textColour);
    const auto& t = currentTheme;

    if (isSeparator)
    {
        g.setColour(t.border);
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour(t.selection);
        g.fillRect(area);
    }

    g.setColour(isActive ? (isHighlighted ? t.textPrimary : t.textSecondary) : t.textDisabled);
    g.setFont(uiFont(juce::jmin(12.5f, static_cast<float>(area.getHeight()) * 0.46f)));

    const int tickWidth = 18;
    if (isTicked)
    {
        g.setColour(t.accent);
        g.fillRect(area.getX() + 6, area.getCentreY() - 2, 4, 4);
    }

    g.drawFittedText(text, area.withTrimmedLeft(tickWidth).withTrimmedRight(hasSubMenu ? 20 : 8),
                     juce::Justification::centredLeft, 1, 0.9f);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(t.textDisabled);
        g.setFont(monoFont(juce::jmin(10.0f, static_cast<float>(area.getHeight()) * 0.40f)));
        g.drawFittedText(shortcutKeyText, area.withTrimmedRight(10),
                         juce::Justification::centredRight, 1, 0.9f);
    }

    if (hasSubMenu)
    {
        const float cx = static_cast<float>(area.getRight() - 12);
        const float cy = static_cast<float>(area.getCentreY());
        juce::Path arrow;
        arrow.startNewSubPath(cx - 2.0f, cy - 3.0f);
        arrow.lineTo(cx + 2.0f, cy);
        arrow.lineTo(cx - 2.0f, cy + 3.0f);
        g.setColour(t.textSecondary);
        g.strokePath(arrow, juce::PathStrokeType(1.3f));
    }
}

void AuroraLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                      int width, int height, bool isScrollbarVertical,
                                      int thumbStartPosition, int thumbSize,
                                      bool isMouseOver, bool isMouseDown)
{
    const auto& t = currentTheme;
    juce::Rectangle<int> thumb;

    if (isScrollbarVertical)
        thumb = { x + width / 3, thumbStartPosition, juce::jmax(3, width / 3), thumbSize };
    else
        thumb = { thumbStartPosition, y + height / 3, thumbSize, juce::jmax(3, height / 3) };

    g.setColour(isMouseDown ? t.accent : (isMouseOver ? t.borderStrong : t.border));
    g.fillRoundedRectangle(thumb.toFloat(), 1.5f);
}
} // namespace aurora
