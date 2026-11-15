#include "LevelMeter.h"

#include "Core/PluginProcessor.h"
#include "UI/AuroraLookAndFeel.h"

namespace aurora
{
namespace
{
constexpr float meterFloorDb = -60.0f;
constexpr float meterTopDb = 0.0f;
}

LevelMeter::LevelMeter(AuroraProcessor& processor, bool showOutputLevel, const Theme& themeToUse,
                       juce::String caption)
    : owner(processor), output(showOutputLevel), theme(themeToUse), label(std::move(caption))
{
    setOpaque(false);
    startTimerHz(30);
}

void LevelMeter::setTheme(const Theme& newTheme)
{
    theme = newTheme;
    repaint();
}

void LevelMeter::resetPeak()
{
    peakDb = meterFloorDb;
    peakHoldFrames = 0;
    repaint();
}

void LevelMeter::timerCallback()
{
    const float target = output ? owner.getOutputPeakDb() : owner.getInputPeakDb();

    if (target > levelDb)
        levelDb = target;
    else
        levelDb = juce::jmax(target, levelDb - 1.6f);

    if (levelDb > peakDb)
    {
        peakDb = levelDb;
        peakHoldFrames = 45;
    }
    else if (--peakHoldFrames <= 0)
    {
        peakDb = juce::jmax(levelDb, peakDb - 0.35f);
    }

    repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto captionArea = bounds.removeFromTop(12);

    juce::Font mono(juce::FontOptions(theme.monoFamily, 8.5f, juce::Font::plain));
    if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
        mono = lf->monoFont(8.5f);

    g.setColour(theme.textSecondary);
    g.setFont(mono);
    g.drawFittedText(label, captionArea, juce::Justification::centred, 1);

    auto meter = bounds.reduced(2, 0).toFloat();
    g.setColour(theme.meterBackground);
    g.fillRect(meter);
    g.setColour(theme.border);
    g.drawRect(meter, 1.0f);

    const int segments = juce::jmax(8, theme.meterSegments);
    const float segmentHeight = meter.getHeight() / static_cast<float>(segments);
    const float gap = juce::jmax(1.0f, segmentHeight * 0.22f);

    const float normalised = juce::jlimit(0.0f, 1.0f,
        (levelDb - meterFloorDb) / (meterTopDb - meterFloorDb));
    const int lit = static_cast<int>(std::round(normalised * static_cast<float>(segments)));

    for (int i = 0; i < segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments - 1);
        const auto colour = t < 0.70f ? theme.meterLow
                          : t < 0.90f ? theme.meterMid
                                      : theme.meterHigh;
        const float y = meter.getBottom() - (static_cast<float>(i + 1)) * segmentHeight + gap * 0.5f;
        juce::Rectangle<float> segment(meter.getX() + 1.0f, y,
                                       meter.getWidth() - 2.0f,
                                       juce::jmax(1.0f, segmentHeight - gap));

        g.setColour(i < lit ? colour : theme.meterBackground.brighter(0.03f));
        g.fillRect(segment);
    }

    if (peakDb > meterFloorDb)
    {
        const float p = juce::jlimit(0.0f, 1.0f, (peakDb - meterFloorDb) / (meterTopDb - meterFloorDb));
        const float y = meter.getBottom() - p * meter.getHeight();
        g.setColour(peakDb >= -0.1f ? theme.danger : theme.textPrimary);
        g.fillRect(juce::Rectangle<float>(meter.getX(), y - 1.0f, meter.getWidth(), 2.0f));
    }
}
} // namespace aurora
