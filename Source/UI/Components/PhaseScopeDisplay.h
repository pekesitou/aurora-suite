#pragma once

#include "Core/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace aurora
{
class AuroraProcessor;

/** Lissajous vectorscope with correlation readout fed by PhaseScope. */
class PhaseScopeDisplay final : public juce::Component,
                                private juce::Timer
{
public:
    PhaseScopeDisplay(AuroraProcessor& processor, const Theme& theme);
    ~PhaseScopeDisplay() override = default;

    void paint(juce::Graphics&) override;
    void setTheme(const Theme& newTheme);

private:
    void timerCallback() override;

    AuroraProcessor& owner;
    Theme theme;

    static constexpr int historySize = 1024;
    std::array<float, historySize> left {};
    std::array<float, historySize> right {};
    int historyCount = 0;
    int historyWrite = 0;

    float correlation = 1.0f;
    std::array<float, 256> incomingLeft {};
    std::array<float, 256> incomingRight {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseScopeDisplay)
};
} // namespace aurora
