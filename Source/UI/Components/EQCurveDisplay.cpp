#include "EQCurveDisplay.h"

#include "Core/PluginProcessor.h"
#include "DSP/DspUtils.h"
#include "UI/AuroraLookAndFeel.h"

#include <cmath>

namespace aurora
{
namespace
{
dsp::BiquadCoeffs makeCoeffs(int type, double fs, double f, double q, double gainDb) noexcept
{
    switch (type)
    {
        case 0: return dsp::BiquadCoeffs::lowPass(fs, f, q);
        case 1: return dsp::BiquadCoeffs::highPass(fs, f, q);
        case 2: return dsp::BiquadCoeffs::bandPass(fs, f, q);
        case 3: return dsp::BiquadCoeffs::notch(fs, f, q);
        case 5: return dsp::BiquadCoeffs::lowShelf(fs, f, q, gainDb);
        case 6: return dsp::BiquadCoeffs::highShelf(fs, f, q, gainDb);
        case 4:
        default: return dsp::BiquadCoeffs::peak(fs, f, q, gainDb);
    }
}

double magnitudeAt(const dsp::BiquadCoeffs& c, double frequency, double sampleRate) noexcept
{
    const double w = dsp::twoPi * frequency / sampleRate;
    const double c1 = std::cos(w);
    const double c2 = std::cos(2.0 * w);

    const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2
                     + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * c1
                     + 2.0 * c.b0 * c.b2 * c2;
    const double den = 1.0 + c.a1 * c.a1 + c.a2 * c.a2
                     + 2.0 * (c.a1 + c.a1 * c.a2) * c1
                     + 2.0 * c.a2 * c2;

    return std::sqrt(juce::jmax(1.0e-12, num) / juce::jmax(1.0e-12, den));
}
} // namespace

EQCurveDisplay::EQCurveDisplay(AuroraProcessor& processor, ModuleId moduleId, const Theme& themeToUse)
    : owner(processor), module(moduleId), theme(themeToUse)
{
    setOpaque(false);
    startTimerHz(15);
}

void EQCurveDisplay::setModule(ModuleId newModuleId)
{
    if (module == newModuleId)
        return;
    module = newModuleId;
    repaint();
}

void EQCurveDisplay::setTheme(const Theme& newTheme)
{
    theme = newTheme;
    repaint();
}

void EQCurveDisplay::refreshNow()
{
    repaint();
}

void EQCurveDisplay::timerCallback()
{
    if (isShowing())
        repaint();
}

float EQCurveDisplay::parameterValue(const juce::String& localId) const
{
    const auto fullId = instancePrefix(module) + "." + localId;
    if (const auto* value = owner.getAPVTS().getRawParameterValue(fullId))
        return value->load(std::memory_order_relaxed);
    return 0.0f;
}

float EQCurveDisplay::responseDb(float frequencyHz) const
{
    const double fs = owner.getSampleRate() > 0.0 ? owner.getSampleRate() : 44100.0;
    double db = static_cast<double>(parameterValue("output"));

    for (int b = 1; b <= 8; ++b)
    {
        const auto prefix = "b" + juce::String(b) + ".";
        if (parameterValue(prefix + "on") < 0.5f)
            continue;

        const int type = static_cast<int>(parameterValue(prefix + "type") + 0.5f);
        const double f = juce::jlimit(20.0, fs * 0.49, static_cast<double>(parameterValue(prefix + "freq")));
        const double g = juce::jlimit(-36.0, 36.0, static_cast<double>(parameterValue(prefix + "gain")));
        const double q = juce::jlimit(0.05, 30.0, static_cast<double>(parameterValue(prefix + "q")));
        db += dsp::gainToDb(magnitudeAt(makeCoeffs(type, fs, f, q, g), frequencyHz, fs));
    }

    return static_cast<float>(juce::jlimit(-48.0, 48.0, db));
}

float EQCurveDisplay::mapFrequencyToX(float frequencyHz) const
{
    const float width = static_cast<float>(getWidth());
    const float logMin = std::log10(20.0f);
    const float logMax = std::log10(20000.0f);
    const float x = (std::log10(juce::jlimit(20.0f, 20000.0f, frequencyHz)) - logMin) / (logMax - logMin);
    return x * width;
}

float EQCurveDisplay::mapDbToY(float db) const
{
    const float height = static_cast<float>(getHeight());
    return height * 0.5f - juce::jlimit(-1.0f, 1.0f, db / 36.0f) * height * 0.46f;
}

void EQCurveDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme.surface);
    g.fillRoundedRectangle(bounds, static_cast<float>(theme.cornerRadius));
    g.setColour(theme.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), static_cast<float>(theme.cornerRadius), 1.0f);

    auto plot = getLocalBounds().reduced(10, 8);
    plot.removeFromTop(12);
    if (plot.getWidth() < 20 || plot.getHeight() < 20)
        return;

    const int steps = static_cast<int>(curveDb.size());
    for (int i = 0; i < steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps - 1);
        const float frequency = 20.0f * std::pow(1000.0f, t);
        curveDb[static_cast<size_t>(i)] = responseDb(frequency);
    }

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(plot);

    juce::Font mono(juce::FontOptions(theme.monoFamily, 8.5f, juce::Font::plain));
    if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
        mono = lf->monoFont(8.5f);

    // Frequency grid.
    const float frequencies[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float f : frequencies)
    {
        const float x = mapFrequencyToX(f);
        g.setColour(theme.grid);
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(plot.getY()), static_cast<float>(plot.getBottom()));
    }

    // dB grid.
    const float dbLines[] = { -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f };
    for (float db : dbLines)
    {
        const float y = mapDbToY(db);
        g.setColour(db == 0.0f ? theme.borderStrong : theme.grid);
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(plot.getX()), static_cast<float>(plot.getRight()));
    }

    juce::Path response;
    for (int i = 0; i < steps; ++i)
    {
        const float x = static_cast<float>(plot.getX())
                      + static_cast<float>(i) / static_cast<float>(steps - 1) * static_cast<float>(plot.getWidth());
        const float y = mapDbToY(curveDb[static_cast<size_t>(i)]);
        if (i == 0)
            response.startNewSubPath(x, y);
        else
            response.lineTo(x, y);
    }

    juce::Path filled(response);
    filled.lineTo(static_cast<float>(plot.getRight()), static_cast<float>(plot.getY() + plot.getHeight() / 2));
    filled.lineTo(static_cast<float>(plot.getX()), static_cast<float>(plot.getY() + plot.getHeight() / 2));
    filled.closeSubPath();
    g.setColour(theme.accent.withAlpha(0.07f));
    g.fillPath(filled);

    g.setColour(theme.accent);
    g.strokePath(response, juce::PathStrokeType(1.7f));

    // Band handles.
    for (int b = 1; b <= 8; ++b)
    {
        const auto prefix = "b" + juce::String(b) + ".";
        const bool on = parameterValue(prefix + "on") >= 0.5f;
        const float frequency = parameterValue(prefix + "freq");
        const float gain = parameterValue(prefix + "gain");
        const int type = static_cast<int>(parameterValue(prefix + "type") + 0.5f);

        const float x = static_cast<float>(plot.getX()) + mapFrequencyToX(frequency) * static_cast<float>(plot.getWidth()) / static_cast<float>(getWidth());
        const float displayGain = (type >= 5) ? gain : (type == 0 || type == 1 || type == 2 || type == 3) ? 0.0f : gain;
        const float y = static_cast<float>(plot.getY()) + mapDbToY(displayGain) * static_cast<float>(plot.getHeight()) / static_cast<float>(getHeight());

        g.setColour(on ? theme.accentSecondary : theme.textDisabled);
        g.fillEllipse(x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        g.setColour(theme.background);
        g.drawEllipse(x - 2.5f, y - 2.5f, 5.0f, 5.0f, 1.0f);
    }

    g.setColour(theme.textSecondary);
    g.setFont(mono);
    g.drawFittedText("EQ RESPONSE", getLocalBounds().reduced(10, 7).removeFromTop(11),
                     juce::Justification::centredLeft, 1);
    g.drawFittedText("20 Hz", plot.withY(plot.getBottom() - 10).withHeight(10),
                     juce::Justification::bottomLeft, 1);
    g.drawFittedText("20 kHz", plot.withY(plot.getBottom() - 10).withHeight(10),
                     juce::Justification::bottomRight, 1);
}
} // namespace aurora
