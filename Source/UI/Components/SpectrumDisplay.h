#pragma once

#include "Core/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace aurora
{
class AuroraProcessor;

/** Log-frequency FFT display fed by the lock-free SpectrumAnalyzer. */
class SpectrumDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    SpectrumDisplay(AuroraProcessor& processor, const Theme& theme);
    ~SpectrumDisplay() override = default;

    void paint(juce::Graphics&) override;
    void setTheme(const Theme& newTheme);

private:
    void timerCallback() override;
    [[nodiscard]] float frequencyToX(float frequencyHz) const;
    [[nodiscard]] float dbToY(float db) const;

    AuroraProcessor& owner;
    Theme theme;
    std::array<float, 2048> bins {};
    int validBins = 0;
    bool hasData = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};
} // namespace aurora
