#include "SpectrumDisplay.h"

#include "Core/PluginProcessor.h"
#include "Metering/SpectrumAnalyzer.h"
#include "UI/AuroraLookAndFeel.h"

#include <cmath>
#include <span>

namespace aurora
{
namespace
{
constexpr float minDb = -96.0f;
constexpr float maxDb = 6.0f;
}

SpectrumDisplay::SpectrumDisplay(AuroraProcessor& processor, const Theme& themeToUse)
    : owner(processor), theme(themeToUse)
{
    setOpaque(false);
    bins.fill(minDb);
    owner.getSpectrumAnalyzer().setPeakHold(true);
    owner.getSpectrumAnalyzer().setAveraging(0.62f);
    startTimerHz(30);
}

void SpectrumDisplay::setTheme(const Theme& newTheme)
{
    theme = newTheme;
    repaint();
}

void SpectrumDisplay::timerCallback()
{
    auto& analyser = owner.getSpectrumAnalyzer();
    const int available = juce::jmin(analyser.getNumBins(), static_cast<int>(bins.size()));
    if (available <= 0)
    {
        repaint();
        return;
    }

    const bool isNew = analyser.readSpectrum(std::span<float>(bins.data(), static_cast<size_t>(available)));
    validBins = available;
    hasData = hasData || isNew;
    repaint();
}

float SpectrumDisplay::frequencyToX(float frequencyHz) const
{
    const float width = static_cast<float>(getWidth());
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(20000.0f);
    return (std::log10(juce::jlimit(20.0f, 20000.0f, frequencyHz)) - logMin)
         / (logMax - logMin) * width;
}

float SpectrumDisplay::dbToY(float db) const
{
    const float height = static_cast<float>(getHeight());
    const float normalised = (juce::jlimit(minDb, maxDb, db) - minDb) / (maxDb - minDb);
    return height - normalised * height;
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme.surface);
    g.fillRoundedRectangle(bounds, static_cast<float>(theme.cornerRadius));
    g.setColour(theme.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), static_cast<float>(theme.cornerRadius), 1.0f);

    auto plot = getLocalBounds().reduced(8, 6);
    plot.removeFromTop(12);
    plot.removeFromBottom(10);
    if (plot.getWidth() < 24 || plot.getHeight() < 20)
        return;

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(plot);

    auto plotY = [&plot](float normalised)
    {
        return static_cast<float>(plot.getBottom()) - juce::jlimit(0.0f, 1.0f, normalised) * static_cast<float>(plot.getHeight());
    };

    // Grid.
    const float frequencies[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float f : frequencies)
    {
        const float x = static_cast<float>(plot.getX())
                      + frequencyToX(f) * static_cast<float>(plot.getWidth()) / static_cast<float>(getWidth());
        g.setColour(theme.grid);
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(plot.getY()), static_cast<float>(plot.getBottom()));
    }

    const float dbLines[] = { -96.0f, -72.0f, -48.0f, -24.0f, 0.0f };
    for (float db : dbLines)
    {
        const float y = plotY((db - minDb) / (maxDb - minDb));
        g.setColour(db == 0.0f ? theme.borderStrong : theme.grid);
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(plot.getX()), static_cast<float>(plot.getRight()));
    }

    if (! hasData || validBins <= 1)
    {
        juce::Font mono(juce::FontOptions(theme.monoFamily, 9.5f, juce::Font::plain));
        if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
            mono = lf->monoFont(9.5f);
        g.setColour(theme.textDisabled);
        g.setFont(mono);
        g.drawFittedText("SPECTRUM  WAITING FOR AUDIO", plot, juce::Justification::centred, 1);
        return;
    }

    const double sampleRate = owner.getSampleRate() > 0.0 ? owner.getSampleRate() : 48000.0;
    const float nyquist = static_cast<float>(sampleRate * 0.5);

    juce::Path path;
    const int points = juce::jmax(2, plot.getWidth());
    for (int i = 0; i < points; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(points - 1);
        const float frequency = 20.0f * std::pow(1000.0f, t);
        const float binPosition = frequency / nyquist * static_cast<float>(validBins - 1);
        const int bin0 = juce::jlimit(0, validBins - 1, static_cast<int>(binPosition));
        const int bin1 = juce::jlimit(0, validBins - 1, bin0 + 1);
        const float frac = binPosition - static_cast<float>(bin0);
        const float db = bins[static_cast<size_t>(bin0)]
                       + frac * (bins[static_cast<size_t>(bin1)] - bins[static_cast<size_t>(bin0)]);

        const float x = static_cast<float>(plot.getX()) + t * static_cast<float>(plot.getWidth());
        const float y = plotY((db - minDb) / (maxDb - minDb));

        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    juce::Path filled(path);
    filled.lineTo(static_cast<float>(plot.getRight()), static_cast<float>(plot.getBottom()));
    filled.lineTo(static_cast<float>(plot.getX()), static_cast<float>(plot.getBottom()));
    filled.closeSubPath();
    g.setColour(theme.accent.withAlpha(0.08f));
    g.fillPath(filled);

    g.setColour(theme.accent);
    g.strokePath(path, juce::PathStrokeType(1.4f));

    juce::Font mono(juce::FontOptions(theme.monoFamily, 8.0f, juce::Font::plain));
    if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
        mono = lf->monoFont(8.0f);

    g.setColour(theme.textSecondary);
    g.setFont(mono);
    g.drawFittedText("SPECTRUM  -96..+6 dBFS", getLocalBounds().reduced(8, 5).removeFromTop(11),
                     juce::Justification::centredLeft, 1);
    g.drawFittedText("20 Hz", plot.withY(plot.getBottom()).withHeight(10),
                     juce::Justification::bottomLeft, 1);
    g.drawFittedText("20 kHz", plot.withY(plot.getBottom()).withHeight(10),
                     juce::Justification::bottomRight, 1);
}
} // namespace aurora
