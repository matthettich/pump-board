#include "UI/MacroStrip.h"
#include "UI/PumpDialLookAndFeel.h"
#include <cmath>

namespace pb
{
using namespace style;
using namespace style::draw;

namespace
{
// A macro knob that answers a right-click with its own menu instead of a drag.
class MacroDial final : public juce::Slider
{
public:
    MacroDial() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox) {}

    std::function<void()> onContextMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu != nullptr)
                onContextMenu();

            return;
        }

        juce::Slider::mouseDown (e);
    }
};
} // namespace

MacroStrip::MacroStrip (MacroBank& bankIn, ParameterRegistry& registryIn,
                        std::function<juce::String (int paramIndex)> labelIn)
    : bank (bankIn), registry (registryIn), labelForParam (std::move (labelIn))
{
    theme.build (accents::machine(), style::isDark());
    dialLookAndFeel = std::make_unique<PumpDialLookAndFeel> (theme);

    for (int i = 0; i < MacroBank::kNumMacros; ++i)
    {
        auto dial = std::make_unique<MacroDial>();
        dial->setLookAndFeel (dialLookAndFeel.get());
        dial->setRange (0.0, 1.0, 0.0);
        dial->setMouseDragSensitivity (200);
        dial->setDoubleClickReturnValue (true, 0.0);

        const int index = bank.macroParamIndex (i);
        dial->setValue (index != ParameterRegistry::kInvalid ? registry.get (index) : 0.0f,
                        juce::dontSendNotification);

        dial->onValueChange = [this, i, index]
        {
            if (index != ParameterRegistry::kInvalid && dials[(size_t) i] != nullptr)
                registry.set (index, (float) dials[(size_t) i]->getValue());
        };

        dial->onContextMenu = [this, i] { showMenu (i); };

        addAndMakeVisible (*dial);
        dials[(size_t) i] = std::move (dial);
    }

    startTimerHz (30);
}

juce::Rectangle<float> MacroStrip::cellBounds (int macro) const
{
    const auto panel = getLocalBounds().toFloat();
    const float w = (panel.getWidth() - 24.0f) / (float) MacroBank::kNumMacros;

    return { panel.getX() + 12.0f + (float) macro * w, panel.getY() + 24.0f, w - 8.0f, 84.0f };
}

void MacroStrip::resized()
{
    for (int i = 0; i < MacroBank::kNumMacros; ++i)
    {
        auto area = cellBounds (i);
        area.removeFromTop (15.0f);                    // the name, painted
        area.removeFromBottom (14.0f);                 // the target count, painted

        const auto side = juce::jmin (area.getWidth(), area.getHeight());
        dials[(size_t) i]->setBounds (area.withSizeKeepingCentre (side, side).getSmallestIntegerContainer());
    }
}

void MacroStrip::paint (juce::Graphics& g)
{
    const auto panel = getLocalBounds().toFloat();

    g.setColour (theme.panel);
    g.fillRect (panel);
    g.setColour (theme.ink);
    g.drawRect (panel.reduced (0.75f), 1.5f);

    draw::caption (g, theme, { panel.getX() + 12.0f, panel.getY() + 9.0f }, "macros");

    for (int i = 0; i < MacroBank::kNumMacros; ++i)
    {
        const auto cell = cellBounds (i);
        const auto& targets = bank.getTargets (i);
        const bool armed = i == pendingMacro;

        draw::text (g, bank.getName (i).toUpperCase(), { cell.getX(), cell.getY(), cell.getWidth(), 13.0f },
                    armed ? theme.neon : theme.ink2, 10.0f, juce::Justification::centred, true, 1.1f);

        juce::String count;

        if (targets.empty())
            count = juce::String (i + 1);
        else
            count = juce::String ((int) targets.size())
                  + (targets.size() == 1 ? " TARGET" : " TARGETS");

        draw::text (g, count, { cell.getX(), cell.getBottom() - 13.0f, cell.getWidth(), 12.0f },
                    armed ? theme.neon : theme.dim, 8.5f, juce::Justification::centred, true, 1.0f);
    }
}

void MacroStrip::timerCallback()
{
    for (int i = 0; i < MacroBank::kNumMacros; ++i)
    {
        const int index = bank.macroParamIndex (i);

        if (index == ParameterRegistry::kInvalid || dials[(size_t) i] == nullptr)
            continue;

        const float value = registry.get (index);

        if (! dials[(size_t) i]->isMouseButtonDown()
            && std::abs ((float) dials[(size_t) i]->getValue() - value) > 1.0e-6f)
            dials[(size_t) i]->setValue (value, juce::dontSendNotification);
    }

    repaint();                                        // the target counts and names change under us
}

void MacroStrip::showMenu (int macro)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (bank.getName (macro) + "   "
                           + juce::String ((int) bank.getTargets (macro).size()) + " TARGETS");
    menu.addItem (1, "Add target...");
    menu.addItem (2, "Rename...");
    menu.addItem (3, "Clear targets", ! bank.getTargets (macro).empty());

    juce::Component::SafePointer<MacroStrip> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dials[(size_t) macro].get()),
                        [safe, macro] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1)
        {
            safe->pendingMacro = macro;
            safe->repaint();

            if (safe->onPickRequested != nullptr)
                safe->onPickRequested (macro);

            return;
        }

        if (result == 2)
        {
            safe->renameMacro (macro);
            return;
        }

        safe->bank.setTargets (macro, {});

        if (safe->onMessage != nullptr)
            safe->onMessage ("TARGETS CLEARED");

        safe->repaint();
    });
}

void MacroStrip::renameMacro (int macro)
{
    auto* window = new juce::AlertWindow ("Macro name", "Name this macro:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", bank.getName (macro));
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MacroStrip> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, macro] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        safe->bank.setName (macro, window->getTextEditorContents ("name"));
        safe->repaint();
    }), true);
}

void MacroStrip::addPickedTarget (int paramIndex)
{
    const int macro = pendingMacro;
    pendingMacro = -1;
    repaint();

    if (macro < 0 || paramIndex == ParameterRegistry::kInvalid)
        return;

    promptForTarget (macro, paramIndex);
}

void MacroStrip::promptForTarget (int macro, int paramIndex)
{
    const auto current = registry.get (paramIndex);
    const auto label = labelForParam != nullptr ? labelForParam (paramIndex) : juce::String();

    // Both ends start at the control's current value: a fresh target moves nothing until the macro is pulled.
    auto* window = new juce::AlertWindow ("Macro target",
                                          (label.isNotEmpty() ? label : juce::String ("THIS CONTROL"))
                                            + "  ->  " + bank.getName (macro),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("min", juce::String (current, 2));
    window->addTextEditor ("max", juce::String (current, 2));

    juce::StringArray curves;
    curves.add ("Linear");
    curves.add ("Exponential");
    curves.add ("Logarithmic");
    curves.add ("Inverted");
    window->addComboBox ("curve", curves, "Shape");

    window->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MacroStrip> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, macro, paramIndex] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        auto targets = safe->bank.getTargets (macro);      // a copy: setTargets replaces the vector

        MacroBank::Target target;
        target.paramIndex = paramIndex;
        target.minValue = window->getTextEditorContents ("min").getFloatValue();
        target.maxValue = window->getTextEditorContents ("max").getFloatValue();

        if (auto* curve = window->getComboBoxComponent ("curve"))
        {
            switch (curve->getSelectedItemIndex())
            {
                case 1:  target.curve = MacroBank::Curve::Exponential; break;
                case 2:  target.curve = MacroBank::Curve::Logarithmic; break;
                case 3:  target.curve = MacroBank::Curve::Inverted;    break;
                default: target.curve = MacroBank::Curve::Linear;      break;
            }
        }

        targets.push_back (target);
        safe->bank.setTargets (macro, std::move (targets));

        if (safe->onMessage != nullptr)
            safe->onMessage ("ADDED TO " + safe->bank.getName (macro));

        safe->repaint();
    }), true);
}
} // namespace pb
