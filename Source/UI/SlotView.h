#pragma once
// Card 2.5, second half: one slot of the board as a real component. It paints a pedal and reports what the
// pointer did; it holds no authoritative state, so the model stays the single source of truth.
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pb
{
class SlotView final : public juce::Component
{
public:
    explicit SlotView (int indexIn);

    void setSlot (const juce::String& name, const juce::String& category,
                  const juce::String& line1, const juce::String& line2, bool on);
    void setSelected (bool shouldBeSelected);
    void setDropTarget (bool shouldBeTarget);
    void setDragging (bool shouldBeDragging);

    // Card 5.24: the slot's share of the block's time budget, 0..1. Repaints only when it moves enough to see.
    void setLoad (float fraction);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // The window decides what each of these means.
    std::function<void (int index)> onToggle, onSelect, onMenu, onRemove;
    std::function<void (int from, juce::Point<int> screenPosition)> onDrag, onDrop;

private:
    juce::Rectangle<float> footswitchBounds() const;
    juce::Rectangle<float> removeBounds() const;      // the little × on a filled pedal, top right

    int index;
    juce::String name, category, line1, line2;
    bool on = true, selected = false, dropTarget = false, dragging = false;
    float load = 0.0f;
    bool armed = false, draggingNow = false;     // a press that has / has not become a drag yet
    bool removeArmed = false, hoveringRemove = false;
    juce::Point<float> pressPoint;
    style::Theme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotView)
};
} // namespace pb
