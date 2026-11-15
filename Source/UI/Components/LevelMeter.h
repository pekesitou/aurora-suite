#pragma once

#include "Core/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace aurora
{
class AuroraProcessor;

/** Flat segmented peak meter with peak-hold marker. */
class LevelMeter final : public juce::Component,
                         private juce::Timer
{
public:
    LevelMeter(AuroraProcessor& processor, bool showOutputLevel, const Theme& theme,
               juce::String caption);

    void paint(juce::Graphics&) override;
    void setTheme(const Theme& newTheme);
    void resetPeak();

private:
    void timerCallback() override;

    AuroraProcessor& owner;
    bool output;
    Theme theme;
    juce::String label;
    float levelDb = -120.0f;
    float peakDb = -120.0f;
    int peakHoldFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};
} // namespace aurora
