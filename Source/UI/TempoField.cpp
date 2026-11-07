#include "UI/TempoField.h"
#include <cmath>

namespace pb
{
using namespace style;

TempoField::TempoField (ParameterRegistry& registryIn)
    : registry (registryIn)
{
    theme.build (accents::machine(), style::isDark());
    tempoIndex = registry.find ("board/tempo");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    startTimerHz (12);
}

void TempoField::timerCallback()
{
    const float value = currentValue();

    if (std::abs (value - shown) > 1.0e-4f)
    {
        shown = value;
        repaint();
    }
}

float TempoField::currentValue() const
{
    return tempoIndex != ParameterRegistry::kInvalid ? registry.get (tempoIndex) : 120.0f;
}

void TempoField::setValue (float bpm)
{
    if (tempoIndex == ParameterRegistry::kInvalid || ! std::isfinite (bpm))
        return;

    registry.set (tempoIndex, juce::jlimit (20.0f, 300.0f, bpm));   // the param clamps too; say it here as well
}

void TempoField::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    draw::keyBox (g, theme, r, false, false, juce::Colour(), 0.0f);

    draw::text (g, juce::String (juce::roundToInt (currentValue())) + " BPM", r.reduced (7.0f, 0.0f),
                theme.ink, 10.5f, juce::Justification::centredLeft, true, 0.8f);
}

void TempoField::mouseDown (const juce::MouseEvent& e)
{
    dragStartValue = currentValue();
    dragStartY = e.position.y;
}

void TempoField::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown())
        return;

    // Up is faster, three pixels to a beat: 20 to 300 fits in one comfortable pull.
    setValue (dragStartValue + (dragStartY - e.position.y) / 3.0f);
}

void TempoField::mouseDoubleClick (const juce::MouseEvent&)
{
    if (tempoIndex == ParameterRegistry::kInvalid)
        return;

    auto* window = new juce::AlertWindow ("Tempo", "Beats per minute (20 to 300):",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("bpm", juce::String (juce::roundToInt (currentValue())));
    window->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result != 1)
            return;

        const auto typed = window->getTextEditorContents ("bpm").trim();

        if (typed.isEmpty())
            return;

        const auto value = typed.getFloatValue();

        if (value > 0.0f)
            setValue (value);
    }), true);
}
} // namespace pb
