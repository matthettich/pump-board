#pragma once
// The board as it should look: pump's Simple skin. Two rows of eight slots, each edge in its
// category's neon, a slot editor, the macro strip and the latency status line.
//
// Style prototype only: the slots are a fixed table so the look can be judged before the engine
// drives it (cards 2.5 and 3.4 replace the table with BoardModel and MacroBank).

#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace pb
{
class BoardPrototype final : public juce::Component
{
public:
    BoardPrototype();
    ~BoardPrototype() override = default;

    void paint (juce::Graphics&) override;

    // 1100 x 700 at 1x. `scale` renders the whole board into a larger image for inspection.
    void renderToPng (const juce::File& out, float scale);

    void setSelectedSlot (int index);

    // The status line's text comes from the live engine once one exists (card 0.2).
    void setStatusText (const juce::String& text);

    // The real contents, once the board has any: painted state follows the engine (a preview of card 2.5).
    void setSlotState (int index, const juce::String& name, const juce::String& category, bool on,
                       const juce::String& line1 = {}, const juce::String& line2 = {});

    // Fired when a footswitch is clicked; the window decides what that means.
    std::function<void (int index)> onSlotToggle;
    // Fired when a slot body (not its footswitch) is clicked.
    std::function<void (int index)> onSlotSelected;
    // Drag a pedal onto another position to move it; the window decides what that means.
    std::function<void (int from, int to)> onSlotMoved;
    // Right-click, or a click on an empty slot's "+": the window offers the pedals to put there.
    std::function<void (int index)> onSlotMenu;

    // Where a real editor panel goes, and whether the painted placeholder is drawn there.
    juce::Rectangle<float> editorBounds() const;
    void setPaintedEditorVisible (bool shouldBeVisible);

    // Where each slot sits; the painted pedals can be switched off once real SlotViews live on top.
    juce::Rectangle<float> getSlotBounds (int index) const { return slotBounds (index); }

    // Where the header's tempo field sits, left of the pedal count.
    juce::Rectangle<float> tempoBounds() const;
    void setPaintedSlotsVisible (bool shouldBeVisible);

    // Where the macro strip sits, so eight real knobs can replace the painted ones (card 3.4).
    juce::Rectangle<float> macroBounds() const;
    void setPaintedMacrosVisible (bool shouldBeVisible);

    // The header's pedal count. It must come from the model, or the board lies about itself.
    void setPedalCount (int count);

    // Card 4.3: the board's name, and a star while it has unsaved changes, in the header where a made-up
    // name used to sit.
    void setPresetName (const juce::String& name, bool hasUnsavedChanges);

    // The header keys that have something behind them. A/B and help had nothing, so they are gone.
    enum HeaderKey { kPrevPreset = 0, kNextPreset, kSave, kBypass };
    std::function<void (int key)> onHeaderKey;

    void setBypassActive (bool shouldBeActive);
    void setStatusRight (const juce::String& text);
    int selectedSlot() const noexcept { return selected; }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct SlotInfo
    {
        juce::String name;      // "" = empty
        juce::String category;  // DRIVE, TIME, SPACE, FILTER, DYNAMICS, MOD, UTIL
        bool on = true;
        juce::String line1, line2;  // the two values silk-screened on the pedal
    };

    juce::Rectangle<float> slotBounds (int index) const;
    juce::Rectangle<float> footswitchBounds (int index) const;
    juce::Rectangle<float> headerKeyBounds (int index) const;   // index into the header key table
    int slotAt (juce::Point<float>) const;
    void paintSlot (juce::Graphics&, int index, const SlotInfo&) const;
    void paintHeader (juce::Graphics&) const;
    void paintEditor (juce::Graphics&) const;
    void paintMacros (juce::Graphics&) const;
    void paintStatus (juce::Graphics&) const;

    std::array<SlotInfo, 16> slots;
    int selected = 2;
    juce::String statusText;
    bool paintedEditor = true;
    bool paintedSlots = true;
    bool paintedMacros = true;
    int pedalCount = 0;
    bool bypassActive = false;
    juce::String presetName { "CLEAN TO CHAOS" };
    bool presetDirty = false;
    juce::String statusRight;      // the board's name, bottom right, where the made-up help hint used to be
    int dragFrom = -1, dragOver = -1;      // the pedal being carried, and the slot under the pointer
    juce::Point<float> dragAnchor;
    bool dragging = false;
    style::Theme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardPrototype)
};
} // namespace pb
