#pragma once
// The board's tempo, where a musician looks for it: in the header. Drag up and down to change it, double-click
// to type a number. It owns no value of its own; the registry's "board/tempo" is the truth.

#include "Control/ParameterRegistry.h"
#include "UI/PumpStyle.h"

namespace pb
{
class TempoField final : public juce::Component, private juce::Timer
{
public:
    explicit TempoField (ParameterRegistry& registryIn);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Card 8.5: the plugin's tempo comes from the host, so the field only shows it. A host-driven field still shows
    // the number (that is how you see the song's BPM has arrived); it just cannot be dragged or typed into.
    void setEditable (bool shouldBeEditable);

private:
    void timerCallback() override;
    float currentValue() const;
    void setValue (float bpm);

    ParameterRegistry& registry;
    int tempoIndex = ParameterRegistry::kInvalid;
    float dragStartValue = 120.0f;
    float dragStartY = 0.0f;
    float shown = -1.0f;            // what is painted; the timer repaints when the value moves elsewhere
    bool editable = true;
    style::Theme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoField)
};
} // namespace pb
