#include "PhaseScopeDisplay.h"

#include "Core/PluginProcessor.h"
#include "Metering/PhaseScope.h"
#include "UI/AuroraLookAndFeel.h"

namespace aurora
{
PhaseScopeDisplay::PhaseScopeDisplay(AuroraProcessor& processor, const Theme& themeToUse)
    : owner(processor), theme(themeToUse)
{
    setOpaque(false);
    startTimerHz(30);
}

void PhaseScopeDisplay::setTheme(const Theme& newTheme)
{
    theme = newTheme;
    repaint();
}

void PhaseScopeDisplay::timerCallback()
{
    const int count = owner.getPhaseScope().readPoints(incomingLeft.data(), incomingRight.data(),
                                                       static_cast<int>(incomingLeft.size()));
    for (int i = 0; i < count; ++i)
    {
        left[static_cast<size_t>(historyWrite)] = incomingLeft[static_cast<size_t>(i)];
        right[static_cast<size_t>(historyWrite)] = incomingRight[static_cast<size_t>(i)];
        historyWrite = (historyWrite + 1) % historySize;
    }
    historyCount = juce::jmin(historySize, historyCount + count);
    correlation = owner.getPhaseScope().correlation();
    repaint();
}

void PhaseScopeDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme.surface);
    g.fillRoundedRectangle(bounds, static_cast<float>(theme.cornerRadius));
    g.setColour(theme.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), static_cast<float>(theme.cornerRadius), 1.0f);

    auto area = getLocalBounds().reduced(8, 6);
    area.removeFromTop(12);
    area.removeFromBottom(14);

    const int side = juce::jmin(area.getWidth(), area.getHeight());
    auto scope = area.withSizeKeepingCentre(side, side);
    if (side < 24)
        return;

    const float cx = static_cast<float>(scope.getCentreX());
    const float cy = static_cast<float>(scope.getCentreY());
    const float radius = static_cast<float>(side) * 0.5f - 2.0f;

    g.setColour(theme.grid);
    g.drawRect(scope.toFloat(), 1.0f);
    g.drawVerticalLine(static_cast<int>(cx), static_cast<float>(scope.getY()), static_cast<float>(scope.getBottom()));
    g.drawHorizontalLine(static_cast<int>(cy), static_cast<float>(scope.getX()), static_cast<float>(scope.getRight()));
    g.setColour(theme.border);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(scope);

    g.setColour(theme.accent.withAlpha(0.82f));
    const float scale = radius * 0.98f;
    for (int i = 0; i < historyCount; ++i)
    {
        const int index = (historyWrite - 1 - i + historySize * 2) % historySize;
        const float l = juce::jlimit(-1.0f, 1.0f, left[static_cast<size_t>(index)]);
        const float r = juce::jlimit(-1.0f, 1.0f, right[static_cast<size_t>(index)]);
        const float x = cx + l * scale;
        const float y = cy - r * scale;
        g.fillRect(x - 0.8f, y - 0.8f, 1.6f, 1.6f);
    }

    juce::Font mono(juce::FontOptions(theme.monoFamily, 8.0f, juce::Font::plain));
    if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
        mono = lf->monoFont(8.0f);

    g.setColour(theme.textSecondary);
    g.setFont(mono);
    g.drawFittedText("PHASE SCOPE", getLocalBounds().reduced(8, 5).removeFromTop(11),
                     juce::Justification::centredLeft, 1);
    g.drawFittedText("L", scope.withX(scope.getX() + 2).withY(scope.getBottom() - 10).withHeight(10),
                     juce::Justification::centredLeft, 1);
    g.drawFittedText("R", scope.withX(scope.getRight() - 10).withY(scope.getY()).withHeight(10),
                     juce::Justification::centredRight, 1);

    const auto correlationColour = correlation < 0.0f ? theme.danger
                                   : correlation < 0.35f ? theme.warning
                                                         : theme.success;
    g.setColour(correlationColour);
    g.setFont(mono);
    g.drawFittedText("CORR " + juce::String(correlation, 2),
                     getLocalBounds().reduced(8, 4).removeFromBottom(11),
                     juce::Justification::centredRight, 1);
}
} // namespace aurora
