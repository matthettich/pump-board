#include "UI/BoardPrototype.h"

namespace pb
{
using namespace style;
using namespace style::draw;

namespace
{
    constexpr float kPad = 22.0f;
    constexpr float kSlotW = 123.0f;
    constexpr float kSlotH = 104.0f;
    constexpr float kSlotGap = 10.0f;
    constexpr float kRowGap = 26.0f;
    constexpr float kBoardTop = 68.0f;
    constexpr float kEditorY = 318.0f;
    constexpr float kEditorH = 176.0f;    constexpr float kMacroY = 510.0f;
    constexpr float kMacroH = 118.0f;

    // The header keys, right to left. `key` is a BoardPrototype::HeaderKey value.
    struct HeaderKeyDef { int key; const char* label; float w; };
    const HeaderKeyDef kHeaderKeys[] = { { 0, "<", 26.0f }, { 1, ">", 26.0f },
                                         { 2, "SAVE", 46.0f }, { 3, "BYPASS", 66.0f } };
}

BoardPrototype::BoardPrototype()
{
    // One skin (IDM pink & green): the palette is the same everywhere, so there is nothing to choose here.
    theme.build();

    selected = 0;                     // slot one is the one that exists (card 2.2)

    slots = {{
        { "pump TUBE",   "DRIVE",    true,  "DRIVE 0.62",  "MIX 100%"     },
        { "pump SNAP",   "DYNAMICS", true,  "ATTACK 0.40", "SUSTAIN 0.55" },
        { "pump SPRING", "SPACE",    true,  "DWELL 0.35",  "MIX 30%"      },
        { "pump ECHO",   "TIME",     false, "TIME 1/8",    "FEEDBACK 45%" },
        { "pump EQ",     "FILTER",   true,  "BANDS 24",    "GAIN +1.5"    },
        { "pump COMP",   "DYNAMICS", true,  "THRESH -18",  "RATIO 4:1"    },
        { "pump SWIRL",  "MOD",      false, "RATE 0.8",    "DEPTH 60%"    },
        { "",            "",         true,  "",            ""             },
        { "pump GATE",   "DYNAMICS", false, "STEPS 16",    "DEPTH 100%"   },
        { "pump VERB",   "SPACE",    true,  "DECAY 2.4",   "MIX 25%"      },
        { "pump CRUSH",  "DRIVE",    false, "BITS 8",      "RATE 22k"     },
        { "pump WIDE",   "UTIL",     true,  "WIDTH 130%",  "MONO 120"     },
        { "pump HEAT",   "DRIVE",    true,  "DRIVE 0.50",  "MIX 80%"      },
        { "pump LIMIT",  "DYNAMICS", true,  "CEIL -0.3",   "GAIN 3.0"     },
        { "",            "",         true,  "",            ""             },
        { "",            "",         true,  "",            ""             }
    }};
}

void BoardPrototype::setSelectedSlot (int index)
{
    selected = juce::jlimit (0, 15, index);
    repaint();
}

void BoardPrototype::setStatusText (const juce::String& text)
{
    if (statusText == text)
        return;

    statusText = text;
    repaint();
}

void BoardPrototype::setSlotState (int index, const juce::String& name, const juce::String& category, bool on,
                                   const juce::String& line1, const juce::String& line2)
{
    if (index < 0 || index >= 16)
        return;

    auto& slot = slots[(size_t) index];

    if (slot.name == name && slot.category == category && slot.on == on
        && slot.line1 == line1 && slot.line2 == line2)
        return;

    slot.name = name;
    slot.category = category;
    slot.on = on;
    slot.line1 = line1;
    slot.line2 = line2;
    repaint();
}

void BoardPrototype::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) std::size (kHeaderKeys); ++i)
    {
        if (! headerKeyBounds (i).contains (e.position))
            continue;

        if (onHeaderKey != nullptr)
            onHeaderKey (kHeaderKeys[i].key);

        return;
    }

    const int index = slotAt (e.position);

    if (index < 0)
        return;

    if (footswitchBounds (index).contains (e.position))
    {
        if (onSlotToggle != nullptr)
            onSlotToggle (index);

        return;
    }

    if (e.mods.isPopupMenu())
    {
        setSelectedSlot (index);

        if (onSlotMenu != nullptr)
            onSlotMenu (index);

        return;
    }

    setSelectedSlot (index);

    if (onSlotSelected != nullptr)
        onSlotSelected (index);

    // An empty slot's "+" is where a pedal gets chosen; a filled one can be carried to another position.
    if (slots[(size_t) index].name.isEmpty())
    {
        if (onSlotMenu != nullptr)
            onSlotMenu (index);

        return;
    }

    dragFrom = index;
    dragAnchor = e.position;
    dragging = false;
}

void BoardPrototype::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0)
        return;

    if (! dragging && e.position.getDistanceFrom (dragAnchor) > 6.0f)
        dragging = true;

    if (! dragging)
        return;

    const int over = slotAt (e.position);

    if (over != dragOver)
    {
        dragOver = over;
        repaint();
    }
}

void BoardPrototype::mouseUp (const juce::MouseEvent&)
{
    const int from = dragFrom, to = dragOver;
    const bool moved = dragging;

    dragFrom = -1;
    dragOver = -1;
    dragging = false;

    if (! moved)
        return;

    repaint();

    if (to >= 0 && to != from && onSlotMoved != nullptr)
        onSlotMoved (from, to);
}

int BoardPrototype::slotAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; ++i)
        if (slotBounds (i).contains (p))
            return i;

    return -1;
}

juce::Rectangle<float> BoardPrototype::editorBounds() const
{
    return { kPad, kEditorY, (float) getWidth() - kPad * 2.0f, kEditorH };
}

void BoardPrototype::setPaintedEditorVisible (bool shouldBeVisible)
{
    paintedEditor = shouldBeVisible;
    repaint();
}

void BoardPrototype::setPaintedSlotsVisible (bool shouldBeVisible)
{
    paintedSlots = shouldBeVisible;
    repaint();
}

void BoardPrototype::setPedalCount (int count)
{
    if (pedalCount == count)
        return;

    pedalCount = count;
    repaint();
}

void BoardPrototype::setPresetName (const juce::String& name, bool hasUnsavedChanges)
{
    const auto shown = name.isNotEmpty() ? name : juce::String ("UNTITLED");

    if (presetName == shown && presetDirty == hasUnsavedChanges)
        return;

    presetName = shown;
    presetDirty = hasUnsavedChanges;
    repaint();
}

void BoardPrototype::setBypassActive (bool shouldBeActive)
{
    if (bypassActive == shouldBeActive)
        return;

    bypassActive = shouldBeActive;
    repaint();
}

void BoardPrototype::setStatusRight (const juce::String& text)
{
    if (statusRight == text)
        return;

    statusRight = text;
    repaint();
}

juce::Rectangle<float> BoardPrototype::slotBounds (int index) const
{
    const int row = index / 8, col = index % 8;
    return { kPad + (float) col * (kSlotW + kSlotGap),
             kBoardTop + (float) row * (kSlotH + kRowGap),
             kSlotW, kSlotH };
}

juce::Rectangle<float> BoardPrototype::footswitchBounds (int index) const
{
    const auto r = slotBounds (index);
    return { r.getX() + 10.0f, r.getBottom() - 32.0f, r.getWidth() - 20.0f, 22.0f };
}

juce::Rectangle<float> BoardPrototype::tempoBounds() const
{
    // Left of the count text, which is 300 wide and right-aligned to the keys.
    return { headerKeyBounds (0).getX() - 406.0f, 15.0f, 96.0f, 22.0f };
}

juce::Rectangle<float> BoardPrototype::headerKeyBounds (int index) const
{
    const juce::Rectangle<float> header { kPad, 12.0f, (float) getWidth() - kPad * 2.0f, 30.0f };

    float right = header.getRight();

    for (int i = (int) std::size (kHeaderKeys) - 1; i >= 0; --i)
    {
        const juce::Rectangle<float> box { right - kHeaderKeys[i].w, header.getY() + 3.0f,
                                           kHeaderKeys[i].w, 24.0f };

        if (i == index)
            return box;

        right -= kHeaderKeys[i].w + 7.0f;
    }

    return {};
}

void BoardPrototype::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
    paintHeader (g);

    if (paintedSlots)
        for (int i = 0; i < 16; ++i)
            paintSlot (g, i, slots[(size_t) i]);

    // the signal runs left to right along the top row, then on along the bottom row
    const auto row1 = slotBounds (0), row2 = slotBounds (8);
    text (g, u8 ("SIGNAL  \u2192"), { row2.getX(), row1.getBottom() + 5.0f, 180.0f, 16.0f },
          theme.dim, 9.5f, juce::Justification::centredLeft, true, 1.2f);
    text (g, u8 ("9 \u2013 16"), { row2.getRight() - 120.0f, row1.getBottom() + 5.0f, 120.0f, 16.0f },
          theme.dim, 9.5f, juce::Justification::centredRight, false);

    if (paintedEditor)
        paintEditor (g);

    paintMacros (g);
    paintStatus (g);
}

void BoardPrototype::paintHeader (juce::Graphics& g) const
{
    const juce::Rectangle<float> header { kPad, 12.0f, (float) getWidth() - kPad * 2.0f, 30.0f };

    // The brand as Thunder's IDM skins set theirs: bold, upper case, tracked, the second word in the accent
    // (Thunder's ".brand .plus" is pink in IDM pink & green).
    {
        const auto brandFont = fonts::bold (20.0f, 1.6f);
        const juce::String first ("PUMP "), second ("BOARD");
        const float firstWidth = juce::GlyphArrangement::getStringWidth (brandFont, first);
        const juce::Rectangle<float> brand { header.getX(), header.getY() - 7.0f, 260.0f, 26.0f };

        g.setFont (brandFont);
        g.setColour (theme.ink);
        g.drawText (first, brand, juce::Justification::centredLeft, false);
        g.setColour (theme.neon);
        g.drawText (second, brand.withTrimmedLeft (firstWidth), juce::Justification::centredLeft, false);
    }

    text (g, "PEDALBOARD", { header.getX() + 2.0f, header.getY() + 18.0f, 260.0f, 14.0f },
          theme.ink2, 9.5f, juce::Justification::centredLeft, true, 1.6f);

    // right-hand keys: only the ones with something behind them
    for (int i = (int) std::size (kHeaderKeys) - 1; i >= 0; --i)
    {
        const auto box = headerKeyBounds (i);
        const bool lit = kHeaderKeys[i].key == kBypass && bypassActive;

        draw::keyBox (g, theme, box, lit, false, lit ? theme.red : juce::Colour(), 0.0f);
        text (g, kHeaderKeys[i].label, box, theme.ink,                   // ink on pink when lit, as Thunder's PLAY
              10.0f, juce::Justification::centred, true, 0.8f);
    }

    const float x = headerKeyBounds (0).getX();

    const auto count = pedalCount <= 0 ? juce::String ("NO PEDALS")
                                       : juce::String (pedalCount) + (pedalCount == 1 ? " PEDAL" : " PEDALS");
    text (g, presetName + (presetDirty ? juce::String ("   *") : juce::String()) + u8 ("   \u00B7   ") + count,
          { x - 300.0f, header.getY(), 292.0f, 30.0f },
          theme.ink2, 10.0f, juce::Justification::centredRight, true, 1.2f);
}

void BoardPrototype::paintSlot (juce::Graphics& g, int index, const SlotInfo& slot) const
{
    const auto r = slotBounds (index);
    const bool isSelected = index == selected;
    const bool empty = slot.name.isEmpty();
    const bool isDropTarget = dragging && dragOver == index && dragFrom != index;
    const bool isDragSource = dragging && dragFrom == index;
    const auto neon = accentForCategory (slot.category);

    // body: panel fill and ink outline, flat (the IDM skin has no offset block)
    g.setColour (empty ? theme.bg : theme.panel);
    g.fillRect (r);

    // the pedal's edge, in its category's neon
    if (! empty)
    {
        g.setColour (slot.on ? neon : withAlpha (neon, 0.35f));
        g.fillRect (r.getX() + 1.5f, r.getY() + 1.5f, r.getWidth() - 3.0f, 3.0f);
    }

    g.setColour (isSelected || isDropTarget ? theme.neon : theme.ink);
    g.drawRect (r.reduced (0.75f), isDropTarget ? 2.5f : (isSelected ? 2.0f : 1.5f));

    if (empty)
    {
        text (g, "+", r.withTrimmedTop (8.0f), theme.dim, 22.0f, juce::Justification::centred, false);
        return;
    }

    text (g, slot.name, { r.getX() + 10.0f, r.getY() + 11.0f, r.getWidth() - 20.0f, 16.0f },
          theme.ink, 11.0f, juce::Justification::centredLeft, true, 0.5f);

    text (g, slot.category, { r.getX() + 10.0f, r.getY() + 28.0f, r.getWidth() - 20.0f, 12.0f },
          slot.on ? neon : withAlpha (neon, 0.45f), 9.0f, juce::Justification::centredLeft, true, 1.2f);

    // the pedal's own values, in mono, the way a silkscreen reads
    text (g, slot.line1, { r.getX() + 10.0f, r.getY() + 47.0f, r.getWidth() - 20.0f, 12.0f },
          withAlpha (theme.ink2, 0.85f), 9.0f, juce::Justification::centredLeft, false);
    text (g, slot.line2, { r.getX() + 10.0f, r.getY() + 60.0f, r.getWidth() - 20.0f, 12.0f },
          withAlpha (theme.ink2, 0.85f), 9.0f, juce::Justification::centredLeft, false);

    // status LED, top right
    const juce::Rectangle<float> led { r.getRight() - 17.0f, r.getY() + 12.0f, 7.0f, 7.0f };
    g.setColour (slot.on ? neon : withAlpha (neon, 0.20f));
    g.fillEllipse (led);
    g.setColour (withAlpha (theme.ink, 0.55f));
    g.drawEllipse (led, 1.0f);

    // footswitch: a key box, filled neon while the pedal is on
    const auto sw = footswitchBounds (index);
    draw::keyBox (g, theme, sw, slot.on, false, slot.on ? neon : juce::Colour(), 3.0f);
    text (g, slot.on ? "ON" : "OFF", sw, slot.on ? theme.neonInk : theme.ink2,
          9.5f, juce::Justification::centred, true, 1.4f);

    // the pedal being carried is left ghosted in its old place until it is dropped
    if (isDragSource)
    {
        g.setColour (withAlpha (theme.bg, 0.6f));
        g.fillRect (r);
    }
}

void BoardPrototype::paintEditor (juce::Graphics& g) const
{
    const juce::Rectangle<float> panel { kPad, kEditorY, (float) getWidth() - kPad * 2.0f, kEditorH };

    g.setColour (theme.panel);
    g.fillRect (panel);
    g.setColour (theme.ink);
    g.drawRect (panel.reduced (0.75f), 1.5f);

    draw::caption (g, theme, { panel.getX() + 12.0f, panel.getY() + 9.0f }, "selected");

    const auto& slot = slots[(size_t) selected];
    const bool empty = slot.name.isEmpty();

    // the editor wears the selected pedal's colour, the way each pump plugin wears its own
    style::Theme ed = theme;
    if (! empty)
        ed.neon = accentForCategory (slot.category);

    text (g, empty ? juce::String ("EMPTY SLOT") : slot.name,
          { panel.getX() + 92.0f, panel.getY() + 9.0f, 220.0f, 15.0f },
          empty ? theme.ink2 : theme.ink, 11.5f, juce::Justification::centredLeft, true, 0.6f);

    if (! empty)
        text (g, slot.category, { panel.getX() + 316.0f, panel.getY() + 9.0f, 140.0f, 15.0f },
              accentForCategory (slot.category), 9.5f, juce::Justification::centredLeft, true, 1.2f);

    // a screen: where the selected effect's own curve or tank is drawn
    const juce::Rectangle<float> screen { panel.getX() + 12.0f, panel.getY() + 32.0f, 420.0f, 130.0f };
    draw::screenBox (g, theme, screen);

    g.saveState();
    g.reduceClipRegion (screen.toNearestInt());
    for (int i = 0; i < 3; ++i)
    {
        juce::Path p;
        const float amp = 22.0f - (float) i * 5.0f;
        const float baseY = screen.getCentreY() + (float) (i - 1) * 32.0f;

        for (float x = 0.0f; x < screen.getWidth(); x += 2.0f)
        {
            const float t = x / screen.getWidth();
            const float y = baseY - std::sin (t * 46.0f + (float) i) * amp * std::exp (-2.4f * t);

            if (x == 0.0f) p.startNewSubPath (screen.getX() + x, y);
            else           p.lineTo (screen.getX() + x, y);
        }

        g.setColour (i == 0 ? accentForCategory (slot.category) : withAlpha (accentForCategory (slot.category), 0.40f));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
    g.restoreState();

    text (g, u8 ("TANK  \u00B7  MEDIUM SPRING"), { screen.getX() + 9.0f, screen.getBottom() - 17.0f, 300.0f, 13.0f },
          theme.ink2, 9.0f, juce::Justification::centredLeft, true, 1.2f);

    // dense knobs, pump's Dial geometry
    const char* labels[] = { "DWELL", "DRIP", "TENSION", "MIX", "LEVEL" };
    const char* values[] = { "0.35", "0.60", "0.45", "30%", "-1.2" };
    const float norms[]  = { 0.35f, 0.60f, 0.45f, 0.30f, 0.62f };

    float x = panel.getRight() - 12.0f - 5.0f * 84.0f;
    for (int i = 0; i < 5; ++i)
    {
        draw::dial (g, ed, { x, panel.getY() + 30.0f, 84.0f, 104.0f },
                    labels[i], norms[i], values[i], 24.0f, false, ! empty);
        x += 84.0f;
    }
}

juce::Rectangle<float> BoardPrototype::macroBounds() const
{
    return { kPad, kMacroY, (float) getWidth() - kPad * 2.0f, kMacroH };
}

void BoardPrototype::setPaintedMacrosVisible (bool shouldBeVisible)
{
    if (paintedMacros == shouldBeVisible)
        return;

    paintedMacros = shouldBeVisible;
    repaint();
}

void BoardPrototype::paintMacros (juce::Graphics& g) const
{
    if (! paintedMacros)
        return;                           // card 3.4: real knobs live there now

    const juce::Rectangle<float> panel { kPad, kMacroY, (float) getWidth() - kPad * 2.0f, kMacroH };

    g.setColour (theme.panel);
    g.fillRect (panel);
    g.setColour (theme.ink);
    g.drawRect (panel.reduced (0.75f), 1.5f);

    draw::caption (g, theme, { panel.getX() + 12.0f, panel.getY() + 9.0f }, "macros");

    const char* names[] = { "WRECK", "SPACE", "GRAIN", "TONE", "PUSH", "AIR", "GLUE", "MIX" };
    const float norms[] = { 0.18f, 0.62f, 0.30f, 0.50f, 0.42f, 0.72f, 0.25f, 0.80f };

    const float w = (panel.getWidth() - 24.0f) / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        const juce::Rectangle<float> cell { panel.getX() + 12.0f + (float) i * w, panel.getY() + 28.0f, w - 8.0f, 84.0f };
        draw::dial (g, theme, cell, names[i], norms[i],
                    juce::String (juce::roundToInt (norms[i] * 100.0f)) + "%", 17.0f, false, true);
    }
}

void BoardPrototype::paintStatus (juce::Graphics& g) const
{
    const float y = (float) getHeight() - 26.0f;

    g.setColour (theme.dim);
    g.fillRect (kPad, y - 8.0f, (float) getWidth() - kPad * 2.0f, 1.0f);

    text (g, statusText.isNotEmpty() ? statusText
                                     : u8 ("48 kHz    64 smp    ROUND TRIP 4.6 ms    LOAD 12%"),
          { kPad, y, 620.0f, 18.0f }, theme.ink2, 10.0f, juce::Justification::centredLeft, false);

    // the board's name, where a help hint that led nowhere used to sit
    text (g, statusRight.isNotEmpty() ? statusRight : juce::String ("NO BOARD LOADED"),
          { (float) getWidth() - kPad - 360.0f, y, 360.0f, 18.0f },
          theme.ink2, 9.5f, juce::Justification::centredRight, true, 1.2f);
}

void BoardPrototype::renderToPng (const juce::File& out, float scale)
{
    const int w = juce::roundToInt ((float) getWidth() * scale);
    const int h = juce::roundToInt ((float) getHeight() * scale);

    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        paintEntireComponent (g, true);
    }

    out.deleteFile();
    if (auto stream = out.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, *stream);
    }
}
} // namespace pb
