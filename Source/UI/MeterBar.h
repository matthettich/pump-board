#pragma once
// Card 0.3: vertical in/out meters from BoardProcessor's atomics, plus the two board trims.
#include "Control/ParameterRegistry.h"
#include "Engine/BoardProcessor.h"
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace pb
{
class MeterBar final : public juce::Component, private juce::Timer
{
public:
    MeterBar (BoardProcessor&, ParameterRegistry&);
    ~MeterBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintMeter (juce::Graphics&, juce::Rectangle<float> area, float db, float holdDb,
                     const juce::String& label) const;

    BoardProcessor& processor;
    ParameterRegistry& registry;
    style::Theme theme;

    std::unique_ptr<juce::LookAndFeel> dialLookAndFeel;   // declared before the sliders it dresses
    juce::Slider inTrim { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider outTrim { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    int inTrimIndex = ParameterRegistry::kInvalid;
    int outTrimIndex = ParameterRegistry::kInvalid;

    float inDb = -60.0f, outDb = -60.0f;
    float inHoldDb = -60.0f, outHoldDb = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBar)
};
} // namespace pb
