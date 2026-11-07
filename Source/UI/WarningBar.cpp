#include "UI/WarningBar.h"

namespace pb
{
using namespace style;

WarningBar::WarningBar()
{
    closeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20202a));
    closeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb8c4dd));
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);
}

void WarningBar::show (const juce::StringArray& messages, const juce::String& newTitle)
{
    title = newTitle;
    lines = messages;
    setVisible (true);
    toFront (true);
    repaint();
}

void WarningBar::resized()
{
    closeButton.setBounds (getWidth() - 104, (getHeight() - 24) / 2, 92, 24);
}

void WarningBar::paint (juce::Graphics& g)
{
    Theme theme;
    theme.build (accents::machine(), style::isDark());

    const auto r = getLocalBounds().toFloat();

    g.setColour (theme.bg);
    g.fillRect (r);
    g.setColour (theme.panel2);
    g.fillRect (r);
    g.setColour (theme.neon);
    g.drawRect (r.reduced (0.75f), 1.5f);

    draw::text (g, title, { r.getX() + 14.0f, r.getY() + 8.0f, r.getWidth() - 140.0f, 14.0f },
                theme.neon, 11.0f, juce::Justification::centredLeft, true, 1.2f);

    float y = r.getY() + 26.0f;

    for (int i = 0; i < lines.size() && i < kMaxLines; ++i)
    {
        draw::text (g, lines[i], { r.getX() + 14.0f, y, r.getWidth() - 140.0f, 14.0f },
                    theme.ink2, 10.0f, juce::Justification::centredLeft, false);
        y += 16.0f;
    }

    if (lines.size() > kMaxLines)
        draw::text (g, "+ " + juce::String (lines.size() - kMaxLines) + " MORE",
                    { r.getX() + 14.0f, y, r.getWidth() - 140.0f, 14.0f },
                    theme.dim, 9.5f, juce::Justification::centredLeft, true, 1.2f);
}
} // namespace pb
