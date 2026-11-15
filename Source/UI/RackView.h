#pragma once

#include "Core/ModuleCatalog.h"
#include "Core/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace aurora
{
class AuroraProcessor;

/**
    Scrollable 16-slot rack.

    A slot is activated/deactivated in place (the DSP instance always exists, so
    automation IDs never change). Slots can be dragged to reorder them; the
    processor forwards the new order to RackEngine through its lock-free command
    queue, which applies a 5 ms fade around the reorder. "+ MODULE" opens a menu
    of every catalog module and toggles its active state.
*/
class RackView final : public juce::Component,
                       private juce::Timer
{
public:
    explicit RackView(AuroraProcessor& processor, const Theme& theme);
    ~RackView() override;

    std::function<void(ModuleId)> onSelectionChanged;

    void selectModule(ModuleId id, bool notify = true);
    [[nodiscard]] ModuleId getSelectedModule() const noexcept { return selected; }
    void setTheme(const Theme& newTheme);
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class List;

    void timerCallback() override;
    void showModuleMenu();
    void moveModule(int from, int to);

    AuroraProcessor& owner;
    Theme theme;
    juce::Viewport viewport;
    std::unique_ptr<List> list;
    juce::TextButton addButton { "+ MODULE" };
    ModuleId selected = ModuleId::utilA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackView)
};
} // namespace aurora
