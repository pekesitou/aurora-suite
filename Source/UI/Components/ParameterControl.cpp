#include "ParameterControl.h"

#include "UI/AuroraLookAndFeel.h"

namespace aurora
{
namespace
{
juce::String formatParameterValue(const ParamSpec& spec, float value)
{
    if (spec.isChoice())
        return spec.choices[static_cast<int>(juce::jlimit(0.0f,
                                                         static_cast<float>(spec.choices.size() - 1),
                                                         value + 0.5f))];

    int decimals = 0;
    if (spec.step > 0.0f && spec.step < 0.001f)      decimals = 3;
    else if (spec.step > 0.0f && spec.step < 0.01f)  decimals = 2;
    else if (spec.step > 0.0f && spec.step < 1.0f)   decimals = 1;

    if (spec.unit == "Hz" && std::abs(value) >= 1000.0f)
        return juce::String(value / 1000.0f, 1) + " kHz";

    if (spec.unit == ":1")
        return juce::String(value, decimals) + ":1";

    auto text = juce::String(value, decimals);
    if (spec.unit.isNotEmpty())
        text << " " << spec.unit;
    return text;
}
} // namespace

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const ParamSpec& parameter,
                                   const Theme& themeToUse)
    : spec(parameter), theme(themeToUse)
{
    setOpaque(false);

    if (spec.isBoolean())
    {
        toggle.setButtonText(spec.name);
        toggle.setTooltip(spec.name);
        addAndMakeVisible(toggle);
        buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, spec.id, toggle);
    }
    else if (spec.isChoice())
    {
        for (int i = 0; i < spec.choices.size(); ++i)
            combo.addItem(spec.choices[i], i + 1);
        combo.setTooltip(spec.name);
        addAndMakeVisible(combo);
        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, spec.id, combo);
    }
    else
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(spec.minValue, spec.maxValue, spec.step > 0.0f ? spec.step : 0.0);
        slider.setDoubleClickReturnValue(true, spec.defaultValue);
        slider.setMouseDragSensitivity(140);

        if (spec.skewCentre > spec.minValue && spec.skewCentre < spec.maxValue)
            slider.setSkewFactorFromMidPoint(spec.skewCentre);

        slider.setTooltip(spec.name + " (" + spec.unit + ")");
        slider.onValueChange = [this] { updateValueText(); repaint(); };
        addAndMakeVisible(slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, spec.id, slider);
    }

    updateValueText();
}

void ParameterControl::paint(juce::Graphics& g)
{
    const bool hover = isMouseOverOrDragging(true);
    auto bounds = getLocalBounds().toFloat();

    g.setColour(hover ? theme.surfaceAlt : theme.surface);
    g.fillRoundedRectangle(bounds, static_cast<float>(theme.cornerRadius));

    g.setColour(selected ? theme.accent : theme.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), static_cast<float>(theme.cornerRadius), 1.0f);

    if (selected)
    {
        g.setColour(theme.accent.withAlpha(0.10f));
        g.fillRoundedRectangle(bounds.reduced(1.0f), static_cast<float>(theme.cornerRadius));
    }

    if (spec.isBoolean())
        return;

    juce::Font labelFont(juce::FontOptions(theme.monoFamily, 9.0f, juce::Font::plain));
    if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
        labelFont = lf->monoFont(9.0f);

    g.setColour(theme.textSecondary);
    g.setFont(labelFont);
    g.drawFittedText(spec.name, getLocalBounds().removeFromTop(13).reduced(4, 0),
                     juce::Justification::centredLeft, 1, 0.80f);

    if (! spec.isChoice())
    {
        juce::Font valueFont = labelFont;
        if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
            valueFont = lf->monoFont(9.5f, true);

        g.setColour(theme.accent);
        g.setFont(valueFont);
        g.drawFittedText(formatParameterValue(spec, slider.getValue()),
                         getLocalBounds().removeFromBottom(13).reduced(3, 0),
                         juce::Justification::centred, 1, 0.85f);
    }
}

void ParameterControl::resized()
{
    auto area = getLocalBounds();

    if (spec.isBoolean())
    {
        toggle.setBounds(area.reduced(5, 3));
        if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
            toggle.setFont(lf->uiFont(compact ? 9.5f : 10.5f));
        return;
    }

    area.removeFromTop(compact ? 12 : 13);
    area.removeFromBottom(compact ? 12 : 13);

    if (spec.isChoice())
    {
        combo.setBounds(area.reduced(3, 1));
        return;
    }

    const int size = juce::jmin(area.getWidth(), area.getHeight());
    slider.setBounds(area.withSizeKeepingCentre(size, size));
}

void ParameterControl::refresh()
{
    if (spec.isBoolean() || spec.isChoice())
        return;

    updateValueText();
}

void ParameterControl::setSelected(bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;
    selected = shouldBeSelected;
    repaint();
}

void ParameterControl::setCompact(bool shouldBeCompact)
{
    if (compact == shouldBeCompact)
        return;
    compact = shouldBeCompact;
    resized();
    repaint();
}

void ParameterControl::updateValueText()
{
    if (spec.isChoice())
        return;
    repaint(getLocalBounds().removeFromBottom(14));
}
} // namespace aurora
