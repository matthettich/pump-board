#include "UI/GenericEffectEditor.h"
#include <cmath>

namespace pb
{
using namespace style;
using namespace style::draw;

namespace
{
bool isWholeNumber (float v)
{
    return std::abs (v - std::round (v)) < 1.0e-4f;
}

// Small integer ranges (a spring model, a note division) step in ones, so a dial cannot land on 2.37.
// 24 wide, so ECHO's 0..17 note divisions and TUBE's 0..3 switches are both integers.
bool stepsInWholeNumbers (const ParamInfo& info)
{
    return (info.maxValue - info.minValue) <= 24.0f
        && isWholeNumber (info.minValue) && isWholeNumber (info.maxValue);
}

// A control that answers a right-click with its own menu instead of starting a drag or a click (card 3.3).
class MappableDial final : public juce::Slider
{
public:
    MappableDial() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox) {}

    std::function<void()> onContextMenu;
    std::function<bool()> onPick;          // card 3.4: true when a macro swallowed the click

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu != nullptr)
                onContextMenu();

            return;
        }

        if (onPick != nullptr && onPick())
            return;

        juce::Slider::mouseDown (e);
    }
};

class MappableKey final : public juce::TextButton
{
public:
    explicit MappableKey (const juce::String& text) : juce::TextButton (text) {}

    std::function<void()> onContextMenu;
    std::function<bool()> onPick;          // card 3.4: true when a macro swallowed the click

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu != nullptr)
                onContextMenu();

            return;
        }

        if (onPick != nullptr && onPick())
            return;

        juce::TextButton::mouseDown (e);
    }
};
} // namespace

juce::String formatParamValue (const ParamInfo& info, float value)
{
    if (! std::isfinite (value))
        value = info.defaultValue;

    if (std::abs (value) < 1.0e-4f)
        value = 0.0f;

    // A named switch reads as its name: "Short tank", not "2". Card 5.23.
    if (const auto label = info.choiceLabel (value); label.isNotEmpty())
        return label;

    const float span = info.maxValue - info.minValue;
    const int decimals = stepsInWholeNumbers (info) ? 0 : (span <= 20.0f ? 2 : 0);

    auto text = juce::String (value, decimals);

    if (info.unit.isNotEmpty())
        text << " " << info.unit;

    return text;
}

GenericEffectEditor::GenericEffectEditor (ParameterRegistry& registryIn)
    : registry (registryIn)
{
    theme.build();
    dialLookAndFeel = std::make_unique<PumpDialLookAndFeel> (theme);
}

GenericEffectEditor::~GenericEffectEditor()
{
    stopTimer();

    for (auto& control : controls)
        if (control.dial != nullptr)
            control.dial->setLookAndFeel (nullptr);
}

void GenericEffectEditor::setSlot (Slot* slot)
{
    current = slot;
    build();

    if (current != nullptr)
        startTimerHz (30);
    else
        stopTimer();
}

void GenericEffectEditor::build()
{
    controls.clear();

    if (current == nullptr || current->getEffect() == nullptr)
    {
        resized();
        repaint();
        return;
    }

    auto* effect = current->getEffect();
    const auto params = effect->params();
    const auto& indices = current->paramIndices();

    for (size_t i = 0; i < params.size(); ++i)
    {
        Control control;
        control.info = params[i];
        control.registryIndex = i < indices.size() ? indices[i] : ParameterRegistry::kInvalid;

        const float value = control.registryIndex != ParameterRegistry::kInvalid
                              ? registry.get (control.registryIndex)
                              : control.info.defaultValue;

        if (control.info.isToggle)
        {
            auto key = std::make_unique<MappableKey> (control.info.name);
            key->setClickingTogglesState (true);

            const int mappedIndex = control.registryIndex;
            const auto mappedInfo = control.info;
            key->onContextMenu = [this, mappedIndex, mappedInfo, anchor = key.get()]
            {
                showParamMenu (mappedIndex, mappedInfo, *anchor);
            };
            key->onPick = [this, mappedIndex] { return pickTarget (mappedIndex); };

            control.toggle = std::move (key);
            control.toggle->setToggleState (value >= 0.5f, juce::dontSendNotification);
            control.toggle->setColour (juce::TextButton::buttonOnColourId, theme.neon);
            control.toggle->setColour (juce::TextButton::buttonColourId, theme.bg);
            control.toggle->setColour (juce::TextButton::textColourOffId, theme.ink2);
            control.toggle->setColour (juce::TextButton::textColourOnId, theme.neonInk);

            const int index = control.registryIndex;
            const float lo = control.info.minValue, hi = control.info.maxValue;
            auto* button = control.toggle.get();
            control.toggle->onClick = [this, index, lo, hi, button]
            {
                if (index != ParameterRegistry::kInvalid)
                    registry.set (index, button->getToggleState() ? hi : lo);
            };

            addAndMakeVisible (*control.toggle);
        }
        else
        {
            auto dial = std::make_unique<MappableDial>();
            dial->setLookAndFeel (dialLookAndFeel.get());

            const int mappedIndex = control.registryIndex;
            const auto mappedInfo = control.info;
            dial->onContextMenu = [this, mappedIndex, mappedInfo, anchor = dial.get()]
            {
                showParamMenu (mappedIndex, mappedInfo, *anchor);
            };
            dial->onPick = [this, mappedIndex] { return pickTarget (mappedIndex); };

            control.dial = std::move (dial);
            control.dial->setMouseDragSensitivity (160);

            const bool wholeNumbers = stepsInWholeNumbers (control.info);
            control.dial->setRange (control.info.minValue, control.info.maxValue, wholeNumbers ? 1.0 : 0.01);

            // Log-scaled params (decay, low cut) put the value the effect calls its midpoint at the
            // knob's midpoint, the same place pump's own dial puts it.
            if (control.info.skewCentre > 0.0f)
                control.dial->setSkewFactorFromMidPoint (control.info.skewCentre);
            control.dial->setDoubleClickReturnValue (true, control.info.defaultValue);
            control.dial->setValue (value, juce::dontSendNotification);

            const int index = control.registryIndex;
            auto* slider = control.dial.get();
            control.dial->onValueChange = [this, index, slider]
            {
                if (index != ParameterRegistry::kInvalid)
                    registry.set (index, (float) slider->getValue());
            };

            addAndMakeVisible (*control.dial);
        }

        controls.push_back (std::move (control));
    }

    resized();
    repaint();
}

void GenericEffectEditor::resized()
{
    auto panel = getLocalBounds().reduced (12);
    panel.removeFromTop (26);                       // the caption row, drawn in paint()

    const int count = (int) controls.size();

    if (count == 0)
        return;

    // Twelve to a row: ECHO has 24 params and the panel is 176 px tall, so seven (four rows) collides.
    const int perRow = juce::jmin (12, count);
    const int rows = (count + perRow - 1) / perRow;
    const int cellWidth = panel.getWidth() / perRow;
    const int rowHeight = panel.getHeight() / juce::jmax (1, rows);

    for (int i = 0; i < count; ++i)
    {
        const int row = i / perRow, col = i % perRow;
        const int inRow = juce::jmin (perRow, count - row * perRow);
        const int rowOffset = (perRow - inRow) * cellWidth / 2;      // centre a short last row

        auto cell = juce::Rectangle<int> (panel.getX() + rowOffset + col * cellWidth,
                                          panel.getY() + row * rowHeight,
                                          cellWidth, rowHeight).reduced (3, 2);
        cell.removeFromTop (13);                    // the name
        cell.removeFromBottom (13);                 // the value

        if (controls[(size_t) i].dial != nullptr)
            controls[(size_t) i].dial->setBounds (cell.reduced (6, 1));
        else if (controls[(size_t) i].toggle != nullptr)
            controls[(size_t) i].toggle->setBounds (cell.withSizeKeepingCentre (juce::jmin (86, cell.getWidth() - 10), 22));
    }
}

void GenericEffectEditor::paint (juce::Graphics& g)
{
    g.setColour (theme.panel);
    g.fillRect (getLocalBounds());
    g.setColour (theme.ink);
    g.drawRect (getLocalBounds().toFloat().reduced (0.75f), 1.5f);

    auto panel = getLocalBounds().reduced (12);
    draw::caption (g, theme, { (float) panel.getX(), (float) panel.getY() }, "selected");

    auto* effect = current != nullptr ? current->getEffect() : nullptr;

    draw::text (g, effect != nullptr ? effect->displayName() : juce::String ("EMPTY SLOT"),
                { (float) panel.getX() + 78.0f, (float) panel.getY(), 340.0f, 15.0f },
                effect != nullptr ? theme.ink : theme.ink2,
                11.5f, juce::Justification::centredLeft, true, 0.6f);

    for (auto& control : controls)
    {
        auto* component = control.dial != nullptr ? (juce::Component*) control.dial.get()
                                                  : (juce::Component*) control.toggle.get();
        const auto b = component->getBounds().toFloat();

        draw::text (g, control.info.name.toUpperCase(), { b.getX(), b.getY() - 13.0f, b.getWidth(), 12.0f },
                    theme.ink2, 9.0f, juce::Justification::centred, true, 1.1f);

        const bool armed = control.registryIndex != ParameterRegistry::kInvalid
                        && control.registryIndex == learnTarget;

        if (armed)
        {
            g.setColour (theme.neon);
            g.drawRect (b.expanded (2.0f), 2.0f);
            draw::text (g, "LEARN", { b.getX(), b.getBottom() + 1.0f, b.getWidth(), 12.0f },
                        theme.neon, 9.5f, juce::Justification::centred, true, 1.2f);
        }
        else if (control.dial != nullptr)
        {
            draw::text (g, formatParamValue (control.info, (float) control.dial->getValue()),
                        { b.getX(), b.getBottom() + 1.0f, b.getWidth(), 12.0f },
                        theme.ink, 9.5f, juce::Justification::centred, false);
        }
    }
}

void GenericEffectEditor::timerCallback()
{
    for (auto& control : controls)
    {
        if (control.registryIndex == ParameterRegistry::kInvalid)
            continue;

        const float value = registry.get (control.registryIndex);

        if (control.dial != nullptr && ! control.dial->isMouseButtonDown())
            control.dial->setValue (value, juce::dontSendNotification);

        if (control.toggle != nullptr)
            control.toggle->setToggleState (value >= 0.5f, juce::dontSendNotification);
    }

    if (! controls.empty())
        repaint();
}

void GenericEffectEditor::setLearnTarget (int paramIndex)
{
    if (learnTarget == paramIndex)
        return;

    learnTarget = paramIndex;
    repaint();
}

void GenericEffectEditor::setPickTargetMode (bool shouldPick)
{
    picking = shouldPick;
}

bool GenericEffectEditor::pickTarget (int registryIndex)
{
    if (! picking)
        return false;

    picking = false;                      // one click, one target

    if (onControlPicked != nullptr && registryIndex != ParameterRegistry::kInvalid)
        onControlPicked (registryIndex);

    return true;
}

void GenericEffectEditor::showParamMenu (int registryIndex, const ParamInfo& info, juce::Component& anchor)
{
    if (registryIndex == ParameterRegistry::kInvalid)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Reset to default");
    menu.addItem (2, "Type a value...", ! info.isToggle);
    menu.addItem (3, "Learn...");

    juce::Component::SafePointer<GenericEffectEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safe, registryIndex, info] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1)
        {
            safe->registry.set (registryIndex, info.defaultValue);
            return;
        }

        if (result == 3)
        {
            if (safe->onLearnRequested != nullptr)
                safe->onLearnRequested (registryIndex);

            return;
        }

        auto* window = new juce::AlertWindow ("Value", info.name + ":", juce::MessageBoxIconType::NoIcon);
        // The raw number, not the display text: a choice label ("Short tank") or a unit string ("1/8") parsed back
        // with getFloatValue() became 0 or 1, so pressing Return on an untouched field changed the parameter.
        const auto initialText = juce::String (safe->registry.get (registryIndex), 3);
        window->addTextEditor ("value", initialText);
        window->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, registryIndex, initialText] (int answer)
        {
            if (answer != 1 || safe == nullptr)
                return;

            const auto typed = window->getTextEditorContents ("value").trim();

            if (typed.isEmpty() || typed == initialText || ! typed.containsAnyOf ("0123456789"))
                return;

            safe->registry.set (registryIndex, typed.getFloatValue());
        }), true);
    });
}
} // namespace pb
