#include "UI/SlotView.h"
#include <cmath>

namespace pb
{
using namespace style;
using namespace style::draw;

SlotView::SlotView (int indexIn) : index (indexIn)
{
    theme.build();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SlotView::setSlot (const juce::String& newName, const juce::String& newCategory,
                        const juce::String& newLine1, const juce::String& newLine2, bool newOn)
{
    if (name == newName && category == newCategory && line1 == newLine1 && line2 == newLine2 && on == newOn)
        return;

    name = newName;
    category = newCategory;
    line1 = newLine1;
    line2 = newLine2;
    on = newOn;
    repaint();
}

void SlotView::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void SlotView::setDropTarget (bool shouldBeTarget)
{
    if (dropTarget == shouldBeTarget)
        return;

    dropTarget = shouldBeTarget;
    repaint();
}

void SlotView::setDragging (bool shouldBeDragging)
{
    if (dragging == shouldBeDragging)
        return;

    dragging = shouldBeDragging;
    repaint();
}

void SlotView::setLoad (float fraction)
{
    const float clamped = juce::jlimit (0.0f, 1.0f, std::isfinite (fraction) ? fraction : 0.0f);

    // a percent of a bar is not worth a repaint on every UI tick
    if (std::abs (clamped - load) < 0.01f)
        return;

    load = clamped;
    repaint();
}

juce::Rectangle<float> SlotView::removeBounds() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getRight() - 22.0f, r.getY() + 5.0f, 16.0f, 16.0f };
}

juce::Rectangle<float> SlotView::footswitchBounds() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getX() + 10.0f, r.getBottom() - 32.0f, r.getWidth() - 20.0f, 22.0f };
}

void SlotView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool empty = name.isEmpty();
    const auto neon = accentForCategory (category);

    // body: panel fill and ink outline, flat (the IDM skin has no offset block)
    g.setColour (empty ? theme.bg : theme.panel);
    g.fillRect (r);

    // the pedal's edge, in its category's neon
    if (! empty)
    {
        g.setColour (on ? neon : withAlpha (neon, 0.35f));
        g.fillRect (r.getX() + 1.5f, r.getY() + 1.5f, r.getWidth() - 3.0f, 3.0f);
    }

    g.setColour (selected || dropTarget ? theme.neon : theme.ink);
    g.drawRect (r.reduced (0.75f), dropTarget ? 2.5f : (selected ? 2.0f : 1.5f));

    if (empty)
    {
        text (g, "+", r.withTrimmedTop (8.0f), theme.dim, 22.0f, juce::Justification::centred, false);
        return;
    }

    text (g, name, { r.getX() + 10.0f, r.getY() + 11.0f, r.getWidth() - 20.0f, 16.0f },
          theme.ink, 11.0f, juce::Justification::centredLeft, true, 0.5f);

    text (g, category, { r.getX() + 10.0f, r.getY() + 28.0f, r.getWidth() - 20.0f, 12.0f },
          on ? neon : withAlpha (neon, 0.45f), 9.0f, juce::Justification::centredLeft, true, 1.2f);

    // the pedal's own values, in mono, the way a silkscreen reads
    text (g, line1, { r.getX() + 10.0f, r.getY() + 47.0f, r.getWidth() - 20.0f, 12.0f },
          withAlpha (theme.ink2, 0.85f), 9.0f, juce::Justification::centredLeft, false);
    text (g, line2, { r.getX() + 10.0f, r.getY() + 60.0f, r.getWidth() - 20.0f, 12.0f },
          withAlpha (theme.ink2, 0.85f), 9.0f, juce::Justification::centredLeft, false);

    // remove: a small × in the pedal's own corner, so taking a pedal off is where the pedal is
    const auto removeArea = removeBounds();
    g.setColour (hoveringRemove ? neon : withAlpha (theme.ink, 0.35f));
    g.drawRect (removeArea.reduced (0.5f), 1.0f);
    {
        const auto inner = removeArea.reduced (4.5f);
        g.drawLine (inner.getTopLeft().x, inner.getTopLeft().y, inner.getBottomRight().x, inner.getBottomRight().y, 1.4f);
        g.drawLine (inner.getTopRight().x, inner.getTopRight().y, inner.getBottomLeft().x, inner.getBottomLeft().y, 1.4f);
    }

    // status LED, left of the × so neither hides the other
    const juce::Rectangle<float> led { r.getRight() - 34.0f, r.getY() + 9.5f, 7.0f, 7.0f };
    g.setColour (on ? neon : withAlpha (neon, 0.20f));
    g.fillEllipse (led);
    g.setColour (withAlpha (theme.ink, 0.55f));
    g.drawEllipse (led, 1.0f);

    // card 5.24: how much of the block this slot spends, a thin bar. Only drawn once it has been measured, so an
    // empty or freshly built slot is unchanged. It sits in the gap between the category line (y 28..40) and the first
    // value line (y 47..59): at bottom - 40 (y 64 on a 104 pt card) it ran through the second value line (y 60..72).
    if (load > 0.0005f)
    {
        const auto bar = juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 42.5f, r.getWidth() - 20.0f, 3.0f);

        g.setColour (withAlpha (theme.ink, 0.16f));
        g.fillRect (bar);
        g.setColour (load > 0.9f ? theme.red : withAlpha (neon, 0.85f));
        g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, load)));
    }

    // footswitch: a key box, filled neon while the pedal is on
    const auto sw = footswitchBounds();
    keyBox (g, theme, sw, on, false, on ? neon : juce::Colour(), 3.0f);
    text (g, on ? "ON" : "OFF", sw, on ? theme.neonInk : theme.ink2,
          9.5f, juce::Justification::centred, true, 1.4f);

    // the pedal being carried is left ghosted in its old place until it lands
    if (dragging)
    {
        g.setColour (withAlpha (theme.bg, 0.6f));
        g.fillRect (r);
    }
}

void SlotView::mouseDown (const juce::MouseEvent& e)
{
    // The × is hit before anything else: a press there must never become a drag.
    if (! name.isEmpty() && removeBounds().contains (e.position))
    {
        if (onSelect != nullptr)
            onSelect (index);

        removeArmed = true;
        return;
    }

    if (footswitchBounds().contains (e.position))
    {
        if (onToggle != nullptr)
            onToggle (index);

        return;
    }

    if (onSelect != nullptr)
        onSelect (index);

    if (e.mods.isPopupMenu())
    {
        if (onMenu != nullptr)
            onMenu (index);

        return;
    }

    // An empty slot's "+" is where a pedal gets chosen; a filled one can be carried to another position.
    if (name.isEmpty())
    {
        if (onMenu != nullptr)
            onMenu (index);

        return;
    }

    armed = true;
    draggingNow = false;
    pressPoint = e.position;
}

void SlotView::mouseDrag (const juce::MouseEvent& e)
{
    if (! armed || removeArmed)
        return;

    if (! draggingNow && e.position.getDistanceFrom (pressPoint) > 6.0f)
    {
        draggingNow = true;
        setDragging (true);
    }

    if (draggingNow && onDrag != nullptr)
        onDrag (index, e.getScreenPosition());
}

void SlotView::mouseMove (const juce::MouseEvent& e)
{
    const bool over = ! name.isEmpty() && removeBounds().contains (e.position);

    if (over != hoveringRemove)
    {
        hoveringRemove = over;
        repaint();
    }
}

void SlotView::mouseExit (const juce::MouseEvent&)
{
    if (hoveringRemove)
    {
        hoveringRemove = false;
        repaint();
    }
}

void SlotView::mouseUp (const juce::MouseEvent& e)
{
    if (removeArmed)
    {
        removeArmed = false;

        // Released inside the × it was pressed on, and only then: sliding off cancels, like any other button.
        if (! name.isEmpty() && removeBounds().contains (e.position) && onRemove != nullptr)
            onRemove (index);

        return;
    }

    const bool wasDragging = draggingNow;

    armed = false;
    draggingNow = false;
    setDragging (false);

    if (wasDragging && onDrop != nullptr)
        onDrop (index, e.getScreenPosition());
}
} // namespace pb
