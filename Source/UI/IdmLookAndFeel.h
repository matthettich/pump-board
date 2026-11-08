#pragma once
// pump BOARD — the app-wide look-and-feel for every stock JUCE widget (buttons, menus, alert windows, the audio
// device selector, list boxes, text fields), dressed in Thunder Plus's IDM PINK & GREEN skin so nothing falls back
// to JUCE's dark default on the light paper. Set once as the default in PumpBoardApplication::initialise.
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pb::style
{
class IdmLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    IdmLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme (
              idm::paper(),    // windowBackground
              idm::paper(),    // widgetBackground
              idm::panel(),    // menuBackground
              idm::ink(),      // outline
              idm::ink(),      // defaultText
              idm::paper(),    // defaultFill
              idm::on(),       // highlightedText
              idm::pink(),     // highlightedFill
              idm::ink()))     // menuText
    {
        using namespace juce;

        setColour (ResizableWindow::backgroundColourId, idm::paper());
        setColour (DocumentWindow::backgroundColourId, idm::paper());

        setColour (TextButton::buttonColourId, idm::paper());
        setColour (TextButton::buttonOnColourId, idm::ink());
        setColour (TextButton::textColourOffId, idm::ink());
        setColour (TextButton::textColourOnId, idm::on());
        setColour (ComboBox::outlineColourId, idm::ink());
        setColour (ToggleButton::tickColourId, idm::pink());
        setColour (ToggleButton::tickDisabledColourId, idm::dim());

        setColour (ComboBox::backgroundColourId, idm::paper());
        setColour (ComboBox::textColourId, idm::ink());
        setColour (ComboBox::arrowColourId, idm::ink());
        setColour (ComboBox::buttonColourId, idm::paper());

        setColour (PopupMenu::backgroundColourId, idm::panel());
        setColour (PopupMenu::textColourId, idm::ink());
        setColour (PopupMenu::headerTextColourId, idm::ink2());
        setColour (PopupMenu::highlightedBackgroundColourId, idm::ink());
        setColour (PopupMenu::highlightedTextColourId, idm::on());

        setColour (AlertWindow::backgroundColourId, idm::panel());
        setColour (AlertWindow::textColourId, idm::ink());
        setColour (AlertWindow::outlineColourId, idm::ink());

        setColour (TextEditor::backgroundColourId, idm::paper());
        setColour (TextEditor::textColourId, idm::ink());
        setColour (TextEditor::outlineColourId, idm::ink());
        setColour (TextEditor::focusedOutlineColourId, idm::pink());
        setColour (TextEditor::highlightColourId, idm::pink().withAlpha (0.35f));
        setColour (CaretComponent::caretColourId, idm::pink());

        setColour (Label::textColourId, idm::ink());
        setColour (ListBox::backgroundColourId, idm::paper());
        setColour (ListBox::textColourId, idm::ink());
        setColour (ListBox::outlineColourId, idm::ink());

        setColour (ScrollBar::thumbColourId, idm::ink2());
        setColour (Slider::thumbColourId, idm::pink());
        setColour (Slider::trackColourId, idm::ink());
        setColour (Slider::backgroundColourId, idm::dim());
        setColour (TooltipWindow::backgroundColourId, idm::ink());
        setColour (TooltipWindow::textColourId, idm::on());
        setColour (TooltipWindow::outlineColourId, idm::ink());
    }

    // JetBrains Mono for every stock widget, as Thunder's IDM skin sets it for every button and field.
    // Only the placeholder faces ("<Sans-Serif>" and friends) are swapped: a font that already names a face (the
    // embedded JetBrains Mono cuts fonts::get builds) is left as it is.
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override
    {
        if (! font.getTypefaceName().startsWithChar ('<'))
            return juce::LookAndFeel_V4::getTypefaceForFont (font);

        auto& tf = font.isBold() ? fonts::boldTypeface() : fonts::monoTypeface();
        return tf != nullptr ? tf : juce::LookAndFeel_V4::getTypefaceForFont (font);
    }

    // Flat and square: the paper (or the explicit button colour) with a 1.5 px ink outline; a toggled-on button is
    // filled with ink, as Thunder's `button.on` is. Hover and press darken a step instead of glowing.
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        // JUCE already hands over buttonOnColourId for a toggled button (ink by default, pink where a panel asks).
        auto fill = background;

        if (down)
            fill = fill.darker (0.12f);
        else if (highlighted)
            fill = fill.darker (0.05f);

        if (! button.isEnabled())
            fill = fill.withMultipliedAlpha (0.5f);

        g.setColour (fill);
        g.fillRect (r);
        g.setColour (button.isEnabled() ? idm::ink() : idm::dim());
        g.drawRect (r.reduced (0.75f), 1.5f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return fonts::get (true, juce::jmin (13.0f, (float) buttonHeight * 0.5f), 0.6f);
    }

    juce::Font getPopupMenuFont() override       { return fonts::get (true, 13.0f); }
    juce::Font getAlertWindowTitleFont() override { return fonts::bold (15.0f); }
    juce::Font getAlertWindowMessageFont() override { return fonts::get (true, 13.0f); }
    juce::Font getAlertWindowFont() override     { return fonts::get (true, 12.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (idm::ink());
        g.drawRect (juce::Rectangle<int> (width, height).toFloat().reduced (0.75f), 1.5f);
    }

    int getPopupMenuBorderSize() override { return 2; }

    // Square, ink-outlined alert box (the stock one is rounded with a drop shadow).
    void drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                       juce::TextLayout& textLayout) override
    {
        g.fillAll (alert.findColour (juce::AlertWindow::backgroundColourId));
        g.setColour (idm::ink());
        g.drawRect (alert.getLocalBounds().toFloat().reduced (0.75f), 1.5f);

        // A pink band across the top: the skin's one loud mark per surface.
        g.setColour (idm::pink());
        g.fillRect (juce::Rectangle<float> (1.5f, 1.5f, (float) alert.getWidth() - 3.0f, 4.0f));

        g.setColour (alert.findColour (juce::AlertWindow::textColourId));
        textLayout.draw (g, textArea.toFloat());
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (r);
        g.setColour (box.isEnabled() ? idm::ink() : idm::dim());
        g.drawRect (r.reduced (0.75f), 1.5f);

        // The down arrow Thunder draws on its drop-downs (a small filled triangle).
        const float s = 4.0f, cx = (float) width - 12.0f, cy = (float) height * 0.5f;
        juce::Path arrow;
        arrow.addTriangle (cx - s, cy - s * 0.5f, cx + s, cy - s * 0.5f, cx, cy + s * 0.7f);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return fonts::get (true, juce::jmin (13.0f, (float) box.getHeight() * 0.55f));
    }
};
} // namespace pb::style
