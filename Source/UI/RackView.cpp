#include "RackView.h"

#include "Core/PluginProcessor.h"
#include "UI/AuroraLookAndFeel.h"

#include <span>

namespace aurora
{
namespace
{
constexpr int slotHeight = 56;
constexpr int rackHeaderHeight = 36;

juce::Rectangle<int> slotBounds(int index, int width)
{
    return { 0, index * slotHeight, width, slotHeight };
}

struct SlotZones
{
    juce::Rectangle<int> body;
    juce::Rectangle<int> power;
    juce::Rectangle<int> up;
    juce::Rectangle<int> down;
};

SlotZones getZones(int index, int width)
{
    auto body = slotBounds(index, width).reduced(6, 2);
    auto zones = SlotZones {};
    zones.power = body.removeFromRight(34).reduced(2, 13);
    auto arrows = body.removeFromRight(18).reduced(4, 9);
    zones.up = arrows.removeFromTop(arrows.getHeight() / 2);
    zones.down = arrows;
    zones.body = body;
    return zones;
}

bool anyControlContains(const SlotZones& zones, juce::Point<int> position)
{
    return zones.power.contains(position) || zones.up.contains(position) || zones.down.contains(position);
}
} // namespace

class RackView::List final : public juce::Component
{
public:
    explicit List(RackView& viewToUse)
        : view(viewToUse)
    {
        setOpaque(true);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = view.theme;
        g.fillAll(theme.background);

        const auto order = view.owner.getRackOrder();

        for (int i = 0; i < kNumModules; ++i)
        {
            const auto id = order[static_cast<size_t>(i)];
            const auto& spec = catalog().module(id);
            const bool active = view.owner.isModuleActive(id);
            const bool selected = view.selected == id;
            const auto zones = getZones(i, getWidth());

            auto fill = selected ? theme.selection : (i == hovered ? theme.surfaceAlt : theme.surface);
            g.setColour(fill);
            g.fillRoundedRectangle(zones.body.toFloat(), static_cast<float>(theme.cornerRadius));

            g.setColour(selected ? theme.accent : theme.border);
            g.drawRoundedRectangle(zones.body.toFloat().reduced(0.5f),
                                   static_cast<float>(theme.cornerRadius), 1.0f);

            // Active state stripe.
            g.setColour(active ? theme.accent : theme.border);
            g.fillRect(juce::Rectangle<int>(zones.body.getX() + 1,
                                            zones.body.getY() + 8,
                                            2,
                                            zones.body.getHeight() - 16));

            juce::Font mono(juce::FontOptions(theme.monoFamily, 8.5f, juce::Font::plain));
            juce::Font monoBold(juce::FontOptions(theme.monoFamily, 9.0f, juce::Font::bold));
            juce::Font ui(juce::FontOptions(theme.uiFamily, 11.5f, juce::Font::plain));
            if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
            {
                mono = lf->monoFont(8.5f);
                monoBold = lf->monoFont(9.0f, true);
                ui = lf->uiFont(11.5f);
            }

            g.setColour(theme.textDisabled);
            g.setFont(mono);
            g.drawFittedText(juce::String(i + 1).paddedLeft('0', 2),
                             zones.body.getX() + 8, zones.body.getY() + 4, 20, 14,
                             juce::Justification::centredLeft, 1);

            const int textX = zones.body.getX() + 30;
            const int textWidth = juce::jmax(20, zones.body.getRight() - textX - 4);

            g.setColour(active ? theme.accent : theme.textDisabled);
            g.setFont(monoBold);
            g.drawFittedText(spec.shortLabel, textX, zones.body.getY() + 5, textWidth, 12,
                             juce::Justification::centredLeft, 1, 0.8f);

            g.setColour(active ? theme.textPrimary : theme.textSecondary);
            g.setFont(ui);
            g.drawFittedText(spec.name, textX, zones.body.getY() + 17, textWidth, 15,
                             juce::Justification::centredLeft, 1, 0.85f);

            juce::String status;
            if (const auto* module = view.owner.getRack().getModule(id))
            {
                const float gr = module->gainReductionDb();
                const int latency = module->latencySamples();
                if (active && gr < -0.05f)
                    status << "GR " << juce::String(gr, 1) << " dB";
                if (latency > 0)
                {
                    if (status.isNotEmpty())
                        status << "   ";
                    status << "LAT " << latency;
                }
            }
            if (status.isNotEmpty())
            {
                g.setColour(theme.textDisabled);
                g.setFont(mono);
                g.drawFittedText(status, textX, zones.body.getBottom() - 16, textWidth, 12,
                                 juce::Justification::centredLeft, 1, 0.8f);
            }

            // Power button.
            g.setColour(active ? theme.accent.withAlpha(0.18f) : theme.surfaceAlt);
            g.fillRoundedRectangle(zones.power.toFloat(), 2.0f);
            g.setColour(active ? theme.accent : theme.border);
            g.drawRoundedRectangle(zones.power.toFloat().reduced(0.5f), 2.0f, 1.0f);
            g.setColour(active ? theme.accent : theme.textDisabled);
            g.setFont(monoBold);
            g.drawFittedText(active ? "ON" : "OFF", zones.power,
                             juce::Justification::centred, 1);

            // Reorder arrows.
            g.setColour(i > 0 ? theme.textSecondary : theme.textDisabled);
            juce::Path up;
            up.startNewSubPath(static_cast<float>(zones.up.getCentreX()) - 4.0f, static_cast<float>(zones.up.getCentreY()) + 2.0f);
            up.lineTo(static_cast<float>(zones.up.getCentreX()), static_cast<float>(zones.up.getCentreY()) - 2.0f);
            up.lineTo(static_cast<float>(zones.up.getCentreX()) + 4.0f, static_cast<float>(zones.up.getCentreY()) + 2.0f);
            g.strokePath(up, juce::PathStrokeType(1.4f));

            g.setColour(i < kNumModules - 1 ? theme.textSecondary : theme.textDisabled);
            juce::Path down;
            down.startNewSubPath(static_cast<float>(zones.down.getCentreX()) - 4.0f, static_cast<float>(zones.down.getCentreY()) - 2.0f);
            down.lineTo(static_cast<float>(zones.down.getCentreX()), static_cast<float>(zones.down.getCentreY()) + 2.0f);
            down.lineTo(static_cast<float>(zones.down.getCentreX()) + 4.0f, static_cast<float>(zones.down.getCentreY()) - 2.0f);
            g.strokePath(down, juce::PathStrokeType(1.4f));
        }

        if (dragging)
        {
            const float y = static_cast<float>(dragTarget * slotHeight);
            g.setColour(theme.accent);
            g.fillRect(juce::Rectangle<float>(6.0f, y - 1.0f,
                                              static_cast<float>(getWidth()) - 12.0f, 2.0f));
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const int index = indexAt(event.y);
        if (index != hovered)
        {
            hovered = index;
            setMouseCursor(index >= 0 ? juce::MouseCursor::PointingHandCursor
                                      : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const int index = indexAt(event.y);
        if (index < 0)
            return;

        const auto order = view.owner.getRackOrder();
        const auto id = order[static_cast<size_t>(index)];
        const auto zones = getZones(index, getWidth());

        if (zones.power.contains(event.getPosition()))
        {
            view.owner.setModuleActive(id, !view.owner.isModuleActive(id));
            view.selectModule(id);
            return;
        }
        if (zones.up.contains(event.getPosition()))
        {
            view.moveModule(index, index - 1);
            return;
        }
        if (zones.down.contains(event.getPosition()))
        {
            view.moveModule(index, index + 1);
            return;
        }

        view.selectModule(id);
        dragSource = index;
        dragTarget = index;
        dragging = false;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragSource < 0)
            return;

        if (! dragging && event.getDistanceFromDragStart() > 6)
            dragging = true;

        if (dragging)
        {
            const int target = indexAt(event.y);
            if (target >= 0 && target != dragTarget)
            {
                dragTarget = target;
                repaint();
            }
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging && dragSource >= 0 && dragTarget >= 0 && dragSource != dragTarget)
            view.moveModule(dragSource, dragTarget);

        dragSource = -1;
        dragTarget = -1;
        dragging = false;
        repaint();
    }

private:
    int indexAt(int y) const
    {
        if (y < 0)
            return -1;
        const int index = y / slotHeight;
        return index >= 0 && index < kNumModules ? index : -1;
    }

    RackView& view;
    int hovered = -1;
    int dragSource = -1;
    int dragTarget = -1;
    bool dragging = false;
};

RackView::RackView(AuroraProcessor& processor, const Theme& themeToUse)
    : owner(processor), theme(themeToUse)
{
    list = std::make_unique<List>(*this);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(list.get(), false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(8);

    addAndMakeVisible(addButton);
    addButton.onClick = [this] { showModuleMenu(); };

    selected = ModuleId::utilA;
    const auto order = owner.getRackOrder();
    for (auto id : order)
    {
        if (owner.isModuleActive(id))
        {
            selected = id;
            break;
        }
    }

    startTimerHz(10);
}

RackView::~RackView()
{
    stopTimer();
    viewport.setViewedComponent(nullptr, false);
    list.reset();
}

void RackView::setTheme(const Theme& newTheme)
{
    theme = newTheme;
    repaint();
    if (list != nullptr)
        list->repaint();
}

void RackView::refresh()
{
    if (list != nullptr)
        list->repaint();
}

void RackView::selectModule(ModuleId id, bool notify)
{
    if (selected == id)
        return;
    selected = id;
    if (list != nullptr)
        list->repaint();
    if (notify && onSelectionChanged)
        onSelectionChanged(id);
}

void RackView::timerCallback()
{
    if (isShowing() && list != nullptr)
        list->repaint();
}

void RackView::showModuleMenu()
{
    juce::PopupMenu menu;
    const auto ids = allModuleIds();

    for (int i = 0; i < static_cast<int>(ids.size()); ++i)
    {
        const auto id = ids[static_cast<size_t>(i)];
        const auto& spec = catalog().module(id);
        const bool active = owner.isModuleActive(id);
        menu.addItem(i + 1, spec.name + "   [" + instancePrefix(id) + "]", true, active);
    }

    auto safe = juce::Component::SafePointer<RackView>(this);
    std::array<ModuleId, kNumModules> capturedIds {};
    std::copy(ids.begin(), ids.end(), capturedIds.begin());

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&addButton).withMinimumWidth(220),
        [safe, capturedIds](int result)
        {
            if (safe == nullptr || result <= 0)
                return;

            const auto id = capturedIds[static_cast<size_t>(result - 1)];
            const bool active = safe->owner.isModuleActive(id);
            safe->owner.setModuleActive(id, !active);
            if (! active)
                safe->selectModule(id);
        });
}

void RackView::moveModule(int from, int to)
{
    if (from == to || from < 0 || from >= kNumModules || to < 0 || to >= kNumModules)
        return;

    auto order = owner.getRackOrder();
    const auto moved = order[static_cast<size_t>(from)];

    if (from < to)
        for (int i = from; i < to; ++i)
            order[static_cast<size_t>(i)] = order[static_cast<size_t>(i + 1)];
    else
        for (int i = from; i > to; --i)
            order[static_cast<size_t>(i)] = order[static_cast<size_t>(i - 1)];

    order[static_cast<size_t>(to)] = moved;
    owner.setRackOrder(std::span<const ModuleId>(order.data(), order.size()));
    selectModule(moved);
}

void RackView::paint(juce::Graphics& g)
{
    g.fillAll(theme.background);

    auto header = getLocalBounds().removeFromTop(rackHeaderHeight);
    g.setColour(theme.border);
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    juce::Font mono(juce::FontOptions(theme.monoFamily, 10.0f, juce::Font::bold));
    if (auto* lf = dynamic_cast<AuroraLookAndFeel*>(&getLookAndFeel()))
        mono = lf->monoFont(10.0f, true);

    g.setColour(theme.textSecondary);
    g.setFont(mono);
    g.drawFittedText("RACK", header.reduced(12, 0),
                     juce::Justification::centredLeft, 1);
}

void RackView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(rackHeaderHeight);
    auto buttonArea = header.removeFromRight(96).reduced(0, 6);
    addButton.setBounds(buttonArea);

    viewport.setBounds(area);
    if (list != nullptr)
    {
        const int width = juce::jmax(120, viewport.getMaximumVisibleWidth());
        list->setSize(width, kNumModules * slotHeight);
    }
}
} // namespace aurora
