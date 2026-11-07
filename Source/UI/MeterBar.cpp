#include "UI/MeterBar.h"
#include "UI/PumpDialLookAndFeel.h"
#include <cmath>

namespace pb
{
namespace
{
// Exact comparison without -Wfloat-equal: the values are copies, so any difference is a real one.
bool differs (float a, float b) noexcept
{
    return a < b || b < a;
}
} // namespace

using namespace style;
using namespace style::draw;

namespace
{
juce::String formatDb (float db)
{
    return (db > 0.0f ? "+" : "") + juce::String (db, 1);
}
} // namespace

MeterBar::MeterBar (BoardProcessor& processorIn, ParameterRegistry& registryIn)
    : processor (processorIn), registry (registryIn)
{
    theme.build();
    dialLookAndFeel = std::make_unique<style::PumpDialLookAndFeel> (theme);

    inTrimIndex = registry.find ("board/inputTrim");
    outTrimIndex = registry.find ("board/outputTrim");

    for (auto* slider : { &inTrim, &outTrim })
    {
        slider->setLookAndFeel (dialLookAndFeel.get());
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider->setRange (-24.0, 12.0, 0.1);
        slider->setMouseDragSensitivity (150);
        slider->setValue (0.0, juce::dontSendNotification);
        addAndMakeVisible (slider);
    }

    if (inTrimIndex != ParameterRegistry::kInvalid)
        inTrim.setValue ((double) registry.get (inTrimIndex), juce::dontSendNotification);

    if (outTrimIndex != ParameterRegistry::kInvalid)
        outTrim.setValue ((double) registry.get (outTrimIndex), juce::dontSendNotification);

    inTrim.onValueChange = [this]
    {
        if (inTrimIndex != ParameterRegistry::kInvalid)
            registry.set (inTrimIndex, (float) inTrim.getValue());
    };

    outTrim.onValueChange = [this]
    {
        if (outTrimIndex != ParameterRegistry::kInvalid)
            registry.set (outTrimIndex, (float) outTrim.getValue());
    };

    startTimerHz (30);
}

MeterBar::~MeterBar()
{
    stopTimer();
    inTrim.setLookAndFeel (nullptr);
    outTrim.setLookAndFeel (nullptr);
}

void MeterBar::timerCallback()
{
    const float inNow = juce::Decibels::gainToDecibels (processor.inputPeak(), -60.0f);
    const float outNow = juce::Decibels::gainToDecibels (processor.outputPeak(), -60.0f);

    // the card's falling peak on the message side: ~27 dB/s at 30 Hz, with a slower hold line
    const float before[] = { inDb, outDb, inHoldDb, outHoldDb };

    inDb = juce::jmax (inNow, inDb - 0.9f);
    outDb = juce::jmax (outNow, outDb - 0.9f);
    inHoldDb = juce::jmax (inNow, inHoldDb - 0.25f);
    outHoldDb = juce::jmax (outNow, outHoldDb - 0.25f);

    // Open item 4: repaint only when something drawn moved. A silent board settles on -60 and then stops.
    bool changed = differs (before[0], inDb) || differs (before[1], outDb)
                || differs (before[2], inHoldDb) || differs (before[3], outHoldDb);

    // the registry is the source of truth: pull the dials back when something else moves them
    if (! inTrim.isMouseButtonDown() && inTrimIndex != ParameterRegistry::kInvalid)
        inTrim.setValue ((double) registry.get (inTrimIndex), juce::dontSendNotification);

    if (! outTrim.isMouseButtonDown() && outTrimIndex != ParameterRegistry::kInvalid)
        outTrim.setValue ((double) registry.get (outTrimIndex), juce::dontSendNotification);

    // the trim read-outs under the dials are painted here too
    const float inTrimNow = (float) inTrim.getValue(), outTrimNow = (float) outTrim.getValue();

    if (differs (inTrimNow, shownInTrim) || differs (outTrimNow, shownOutTrim))
    {
        shownInTrim = inTrimNow;
        shownOutTrim = outTrimNow;
        changed = true;
    }

    if (changed)
        repaint();
}

void MeterBar::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (24);

    auto dials = r.removeFromBottom (176);
    inTrim.setBounds (dials.removeFromLeft (dials.getWidth() / 2).reduced (10, 26));
    outTrim.setBounds (dials.reduced (10, 26));
}

void MeterBar::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRect (getLocalBounds());
    g.setColour (theme.ink);
    g.drawRect (getLocalBounds().toFloat().reduced (0.75f), 1.5f);

    auto r = getLocalBounds().reduced (12);
    draw::caption (g, theme, { (float) r.getX(), (float) r.getY() }, "meters");
    r.removeFromTop (24);

    auto metersArea = r.withTrimmedBottom (170);
    auto left = metersArea.removeFromLeft (metersArea.getWidth() / 2);

    paintMeter (g, left.reduced (8, 4).toFloat(), inDb, inHoldDb, "IN");
    paintMeter (g, metersArea.reduced (8, 4).toFloat(), outDb, outHoldDb, "OUT");

    // the dials' own labels and values, in the pump idiom
    const auto labelIn = inTrim.getBounds();
    const auto labelOut = outTrim.getBounds();

    draw::text (g, "IN TRIM", { (float) labelIn.getX(), (float) labelIn.getY() - 18.0f,
                                (float) labelIn.getWidth(), 14.0f },
                theme.ink2, 9.5f, juce::Justification::centred, true, 1.2f);
    draw::text (g, "OUT TRIM", { (float) labelOut.getX(), (float) labelOut.getY() - 18.0f,
                                 (float) labelOut.getWidth(), 14.0f },
                theme.ink2, 9.5f, juce::Justification::centred, true, 1.2f);

    draw::text (g, formatDb ((float) inTrim.getValue()) + " dB",
                { (float) labelIn.getX(), (float) labelIn.getBottom(), (float) labelIn.getWidth(), 14.0f },
                theme.ink, 10.0f, juce::Justification::centred, false);
    draw::text (g, formatDb ((float) outTrim.getValue()) + " dB",
                { (float) labelOut.getX(), (float) labelOut.getBottom(), (float) labelOut.getWidth(), 14.0f },
                theme.ink, 10.0f, juce::Justification::centred, false);
}

void MeterBar::paintMeter (juce::Graphics& g, juce::Rectangle<float> area, float db, float holdDb,
                           const juce::String& label) const
{
    draw::text (g, label, area.removeFromTop (16.0f), theme.ink2, 10.0f,
                juce::Justification::centred, true, 1.2f);

    auto valueArea = area.removeFromBottom (18.0f);

    g.setColour (theme.bg);
    g.fillRect (area);
    g.setColour (theme.dim);
    g.drawRect (area.reduced (0.75f), 1.5f);

    const auto inner = area.reduced (3.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (norm > 0.0f)
    {
        g.setColour (db >= -3.0f ? theme.red : theme.second);   // green while safe, pink when hot
        g.fillRect (inner.withTrimmedTop (inner.getHeight() * (1.0f - norm)));
    }

    {   // peak hold
        const float holdNorm = juce::jlimit (0.0f, 1.0f, (holdDb + 60.0f) / 60.0f);
        const float y = inner.getBottom() - holdNorm * inner.getHeight();

        if (holdNorm > 0.0f)
        {
            g.setColour (theme.ink);
            g.fillRect (inner.getX(), y - 1.0f, inner.getWidth(), 1.5f);
        }
    }

    for (const float tickDb : { 0.0f, -12.0f, -24.0f, -48.0f })
    {
        const float t = juce::jlimit (0.0f, 1.0f, (tickDb + 60.0f) / 60.0f);
        g.setColour (withAlpha (theme.ink, 0.45f));
        g.fillRect (inner.getX(), inner.getBottom() - t * inner.getHeight(), 7.0f, 1.0f);
    }

    draw::text (g, db <= -59.5f ? u8 ("-inf") : juce::String (db, 1),
                valueArea, theme.ink, 10.5f, juce::Justification::centred, false);
}
} // namespace pb
