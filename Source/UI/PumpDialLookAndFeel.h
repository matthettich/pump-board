#pragma once
// pump's Dial as a juce::LookAndFeel, so any plain juce::Slider can wear it.
// Lifted out of MeterBar.cpp when the effect editor needed the same knob (card 1.5).
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pb::style
{
class PumpDialLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit PumpDialLookAndFeel (Theme t) : theme (t) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, juce::Slider&) override
    {
        auto r = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const auto centre = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 4.0f;

        if (rad < 6.0f)
            return;

        // pump's Dial geometry: 270 degrees from 0.75 pi, pointer at the value angle.
        const float a0 = juce::MathConstants<float>::pi * 0.75f;
        const float a1 = juce::MathConstants<float>::pi * 2.25f;
        const float to = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, sliderPos);

        draw::arc (g, centre, rad, a0, a1, 4.0f, theme.dim);
        draw::arc (g, centre, rad, a0, to, 4.0f, theme.neon);

        g.setColour (theme.bg);
        g.fillEllipse (centre.x - (rad - 5.0f), centre.y - (rad - 5.0f), (rad - 5.0f) * 2.0f, (rad - 5.0f) * 2.0f);
        g.setColour (theme.ink);
        g.drawEllipse (centre.x - (rad - 5.0f), centre.y - (rad - 5.0f), (rad - 5.0f) * 2.0f, (rad - 5.0f) * 2.0f, 1.5f);

        const juce::Point<float> dir { std::cos (to), std::sin (to) };
        g.setColour (theme.ink);
        g.drawLine (juce::Line<float> (centre.x + dir.x * (rad - 14.0f) * 0.25f,
                                       centre.y + dir.y * (rad - 14.0f) * 0.25f,
                                       centre.x + dir.x * (rad - 6.5f),
                                       centre.y + dir.y * (rad - 6.5f)), 2.2f);
    }

private:
    Theme theme;
};
} // namespace pb::style
