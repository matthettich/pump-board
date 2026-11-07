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

    const void* effect = slot != nullptr ? (const void*) slot->getEffect() : nullptr;

    if (slot != builtFor || effect != builtEffect)
        page = 0;

    builtFor = slot;
    builtEffect = effect;
    build();

    if (current != nullptr)
        startTimerHz (30);
    else
        stopTimer();
}

void GenericEffectEditor::build()
{
    controls.clear();
    pages.clear();

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

    paginate();
    resized();
    repaint();
}

namespace
{
juce::String idGroup (const juce::String& id)
{
    return id.contains ("_") ? id.upToFirstOccurrenceOf ("_", false, false) : juce::String();
}
} // namespace

// Pages are contiguous ranges in the pedal's natural order. The page count is the fewest that fits kMaxPerPage;
// the cut points are then chosen (dynamic programming) to keep pages near-equal in size and, at a small cost per
// miss, to fall where the id prefix changes (beast_ / split_ / fx_ ...), so a group is not torn in half.
void GenericEffectEditor::paginate()
{
    pages.clear();
    const int n = (int) controls.size();

    if (n == 0)
    {
        page = 0;
        return;
    }

    std::vector<bool> groupStart ((size_t) n + 1, true);       // a page may begin here without splitting a group

    for (int i = 1; i < n; ++i)
        groupStart[(size_t) i] = idGroup (controls[(size_t) i].info.id) != idGroup (controls[(size_t) i - 1].info.id);

    const int count = (n + kMaxPerPage - 1) / kMaxPerPage;
    const double ideal = (double) n / count;
    const double inf = 1.0e18;

    // best[k][i]: least cost of laying the first i controls on k pages; from[k][i]: where page k began.
    std::vector<std::vector<double>> best ((size_t) count + 1, std::vector<double> ((size_t) n + 1, inf));
    std::vector<std::vector<int>> from ((size_t) count + 1, std::vector<int> ((size_t) n + 1, 0));
    best[0][0] = 0.0;

    for (int k = 1; k <= count; ++k)
        for (int i = 1; i <= n; ++i)
            for (int j = juce::jmax (0, i - kMaxPerPage); j < i; ++j)
            {
                if (best[(size_t) k - 1][(size_t) j] >= inf)
                    continue;

                const double d = (double) (i - j) - ideal;
                const double cost = best[(size_t) k - 1][(size_t) j] + d * d + (groupStart[(size_t) j] ? 0.0 : 30.0);

                if (cost < best[(size_t) k][(size_t) i])
                {
                    best[(size_t) k][(size_t) i] = cost;
                    from[(size_t) k][(size_t) i] = j;
                }
            }

    std::vector<int> cuts;                                      // page ends, last first

    for (int k = count, i = n; k >= 1; --k)
    {
        cuts.push_back (i);
        i = from[(size_t) k][(size_t) i];
    }

    int begin = 0;

    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
    {
        // A group names the page only when it holds at least three of its controls: a lone "fft_size" on a page of
        // unprefixed vocoder controls made the page read "FFT".
        juce::StringArray shown;

        for (int i = begin; i < *it; ++i)
        {
            const auto group = idGroup (controls[(size_t) i].info.id);

            if (group.isEmpty() || shown.contains (group))
                continue;

            int members = 0;
            for (int j = begin; j < *it; ++j)
                if (idGroup (controls[(size_t) j].info.id) == group)
                    ++members;

            if (members >= 3)
                shown.add (group);
        }

        const bool tooMany = shown.size() > 3;
        shown.removeRange (3, shown.size());
        pages.push_back ({ begin, *it, shown.joinIntoString ("/").toUpperCase() + (tooMany ? "/..." : "") });
        begin = *it;
    }

    page = juce::jlimit (0, (int) pages.size() - 1, page);

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const bool onPage = (int) i >= pages[(size_t) page].begin && (int) i < pages[(size_t) page].end;

        if (controls[i].dial != nullptr)  controls[i].dial->setVisible (onPage);
        if (controls[i].toggle != nullptr) controls[i].toggle->setVisible (onPage);
    }
}

void GenericEffectEditor::setPage (int newPage)
{
    if (pages.empty())
        return;

    newPage = juce::jlimit (0, (int) pages.size() - 1, newPage);

    if (newPage == page)
        return;

    page = newPage;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const bool onPage = (int) i >= pages[(size_t) page].begin && (int) i < pages[(size_t) page].end;

        if (controls[i].dial != nullptr)  controls[i].dial->setVisible (onPage);
        if (controls[i].toggle != nullptr) controls[i].toggle->setVisible (onPage);
    }

    resized();
    repaint();
}

juce::Rectangle<int> GenericEffectEditor::pageKeyBounds (int pageIndex) const
{
    constexpr int w = 22, h = 15, gap = 4;
    const auto panel = getLocalBounds().reduced (12);
    const int total = (int) pages.size();
    return { panel.getRight() - total * w - (total - 1) * gap + pageIndex * (w + gap), panel.getY(), w, h };
}

int GenericEffectEditor::pageKeyAt (juce::Point<int> p) const
{
    if (pages.size() < 2)
        return -1;

    for (int i = 0; i < (int) pages.size(); ++i)
        if (pageKeyBounds (i).expanded (2).contains (p))
            return i;

    return -1;
}

void GenericEffectEditor::mouseDown (const juce::MouseEvent& e)
{
    if (const int hit = pageKeyAt (e.getPosition()); hit >= 0)
        setPage (hit);
}

namespace
{
// Open item 6. The panel is a fixed 176 px tall, so a pedal with many params cannot simply be squeezed into
// one row (every knob shrinks to the width of its cell) or four (it collides). Instead, try one to three rows
// of balanced columns and keep the layout that gives the biggest knob, as long as a name still fits its cell.
constexpr int kLabelRow = 12;            // the name above and the value below a knob
constexpr int kMinCellForName = 66;      // ~11 characters of the 9 pt label; longer names ("HARMONY 1 LEVEL") need 2 rows
constexpr int kMinDial = 40;             // dense pedals should never go below this when there is room for it

struct GridChoice
{
    int rows = 1, perRow = 1, dial = 0;
};

GridChoice chooseGrid (int count, int width, int height, int longestName)
{
    GridChoice best;
    bool haveBest = false, bestFits = false;

    for (int rows = 1; rows <= 3; ++rows)
    {
        const int perRow = (count + rows - 1) / rows;
        const int cellWidth = width / juce::jmax (1, perRow);
        const int rowHeight = height / rows;
        const int dial = juce::jmin (cellWidth - 10, rowHeight - 2 * kLabelRow - 4);
        const bool fits = cellWidth >= juce::jmax (kMinCellForName, (int) (longestName * 5.3f) + 8);

        // Layouts whose names fit beat those that do not; among equals the bigger knob wins (fewer rows on a tie).
        if (! haveBest || (fits && ! bestFits) || (fits == bestFits && dial > best.dial))
        {
            best = { rows, perRow, dial };
            haveBest = true;
            bestFits = fits;
        }
    }

    return best;
}
} // namespace

void GenericEffectEditor::resized()
{
    auto panel = getLocalBounds().reduced (12);
    panel.removeFromTop (26);                       // the caption row, drawn in paint()

    if (controls.empty() || pages.empty())
        return;

    const int first = pages[(size_t) page].begin;
    const int count = pages[(size_t) page].end - first;

    int longestName = 0;                            // the 9 pt label is ~5.3 px a character, tracking included

    for (int n = 0; n < count; ++n)
        longestName = juce::jmax (longestName, controls[(size_t) (first + n)].info.name.length());

    auto grid = chooseGrid (count, panel.getWidth(), panel.getHeight(), longestName);

    if (grid.dial < kMinDial && count > 12)
    {
        // Dense pedal and still small: borrow the gap under the caption, the one place with nothing in it.
        panel.setTop (panel.getY() - 8);
        grid = chooseGrid (count, panel.getWidth(), panel.getHeight(), longestName);
    }

    const int perRow = grid.perRow;
    const int rows = grid.rows;
    const int cellWidth = panel.getWidth() / perRow;
    const int rowHeight = panel.getHeight() / juce::jmax (1, rows);

    for (int n = 0; n < count; ++n)
    {
        const int i = first + n;
        const int row = n / perRow, col = n % perRow;
        const int inRow = juce::jmin (perRow, count - row * perRow);
        const int rowOffset = (perRow - inRow) * cellWidth / 2;      // centre a short last row

        auto cell = juce::Rectangle<int> (panel.getX() + rowOffset + col * cellWidth,
                                          panel.getY() + row * rowHeight,
                                          cellWidth, rowHeight).reduced (3, 1);
        cell.removeFromTop (kLabelRow);             // the name
        cell.removeFromBottom (kLabelRow);          // the value

        if (controls[(size_t) i].dial != nullptr)
            controls[(size_t) i].dial->setBounds (cell.reduced (2, 0));
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

    if (pages.size() > 1)
    {
        const auto firstKey = pageKeyBounds (0);
        draw::text (g, pages[(size_t) page].label,
                    { (float) firstKey.getX() - 214.0f, (float) firstKey.getY(), 206.0f, (float) firstKey.getHeight() },
                    theme.ink2, 9.5f, juce::Justification::centredRight, true, 1.1f);

        for (int i = 0; i < (int) pages.size(); ++i)
        {
            const auto r = pageKeyBounds (i).toFloat();
            const bool on = i == page;
            draw::keyBox (g, theme, r, on);
            draw::text (g, juce::String (i + 1), r, on ? theme.bg : theme.ink, 10.0f,
                        juce::Justification::centred, true, 0.0f);
        }
    }

    const int shownFirst = pages.empty() ? 0 : pages[(size_t) page].begin;
    const int shownEnd = pages.empty() ? 0 : pages[(size_t) page].end;

    for (int ci = shownFirst; ci < shownEnd; ++ci)
    {
        auto& control = controls[(size_t) ci];
        auto* component = control.dial != nullptr ? (juce::Component*) control.dial.get()
                                                  : (juce::Component*) control.toggle.get();
        const auto b = component->getBounds().toFloat();

        draw::text (g, control.info.name.toUpperCase(), { b.getX(), b.getY() - 12.0f, b.getWidth(), 11.0f },
                    theme.ink2, 9.0f, juce::Justification::centred, true, 1.1f);

        const bool armed = control.registryIndex != ParameterRegistry::kInvalid
                        && control.registryIndex == learnTarget;

        if (armed)
        {
            g.setColour (theme.neon);
            g.drawRect (b.expanded (2.0f), 2.0f);
            draw::text (g, "LEARN", { b.getX(), b.getBottom() + 1.0f, b.getWidth(), 11.0f },
                        theme.neon, 9.5f, juce::Justification::centred, true, 1.2f);
        }
        else if (control.dial != nullptr)
        {
            draw::text (g, formatParamValue (control.info, (float) control.dial->getValue()),
                        { b.getX(), b.getBottom() + 1.0f, b.getWidth(), 11.0f },
                        theme.ink, 9.5f, juce::Justification::centred, false);
        }
    }
}

void GenericEffectEditor::timerCallback()
{
    // Open item 4: repaint only when a value the panel draws moved (the knobs repaint themselves when set).
    bool changed = false;

    for (auto& control : controls)
    {
        if (control.registryIndex == ParameterRegistry::kInvalid)
            continue;

        const float value = registry.get (control.registryIndex);

        if (! control.shownValid || value < control.shownValue || control.shownValue < value)
        {
            control.shownValue = value;
            control.shownValid = true;
            changed = true;
        }

        if (control.dial != nullptr && ! control.dial->isMouseButtonDown())
            control.dial->setValue (value, juce::dontSendNotification);

        if (control.toggle != nullptr)
            control.toggle->setToggleState (value >= 0.5f, juce::dontSendNotification);
    }

    if (changed)
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
