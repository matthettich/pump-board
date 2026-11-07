#include "UI/MappingsView.h"

namespace pb
{
using namespace style;
using namespace style::draw;

class MappingsView::Model final : public juce::ListBoxModel
{
public:
    Model (MappingsView& ownerIn, LearnController& controllerIn)
        : owner (ownerIn), controller (controllerIn)
    {
    }

    int getNumRows() override { return (int) controller.mappings().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto& mappings = controller.mappings();

        if (row < 0 || row >= (int) mappings.size())
            return;

        const auto& mapping = mappings[(size_t) row];

        g.setColour (owner.theme.bg.withAlpha (row % 2 == 0 ? 0.45f : 0.20f));
        g.fillRect (0, 0, width, height);

        g.setColour (owner.theme.dim);
        g.fillRect (0, height - 1, width, 1);

        const int colSource = width * 30 / 100;
        const int colAction = width * 20 / 100;
        const int colTarget = width * 30 / 100;
        const int colRange = width - colSource - colAction - colTarget;

        text (g, controller.describeSource (mapping.source),
              { 8.0f, 0.0f, (float) colSource - 12.0f, (float) height },
              owner.theme.ink, 10.5f, juce::Justification::centredLeft, true, 0.6f);

        text (g, controller.describeAction (mapping.action),
              { (float) colSource, 0.0f, (float) colAction - 6.0f, (float) height },
              owner.theme.neon, 10.0f, juce::Justification::centredLeft, true, 0.8f);

        text (g, controller.describeTarget (mapping.paramIndex),
              { (float) (colSource + colAction), 0.0f, (float) colTarget - 6.0f, (float) height },
              owner.theme.ink, 10.5f, juce::Justification::centredLeft, true, 0.5f);

        juce::String range;

        if (mapping.action == ActionType::SetValue || mapping.action == ActionType::Momentary
            || mapping.action == ActionType::CCThreshold)
            range = juce::String (mapping.minValue, 2) + " .. " + juce::String (mapping.maxValue, 2);

        text (g, range, { (float) (colSource + colAction + colTarget), 0.0f, (float) colRange - 4.0f, (float) height },
              owner.theme.ink2, 9.5f, juce::Justification::centredLeft, false);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        owner.showRowMenu (row);
    }

private:
    MappingsView& owner;
    LearnController& controller;
};

MappingsView::MappingsView (LearnController& controllerIn)
    : controller (controllerIn)
{
    theme.build();

    model = std::make_unique<Model> (*this, controller);
    list.setModel (model.get());
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    closeButton.setColour (juce::TextButton::buttonColourId, pb::style::idm::paper());
    closeButton.setColour (juce::TextButton::textColourOffId, pb::style::idm::ink());
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    refresh();
}

MappingsView::~MappingsView()
{
    list.setModel (nullptr);             // the list outlives its model by one member, but be explicit
}

void MappingsView::refresh()
{
    list.updateContent();
    list.repaint();
    repaint();
}

void MappingsView::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (34);             // the caption row, painted
    area.removeFromTop (16);             // the column captions, painted

    closeButton.setBounds (area.removeFromBottom (40).removeFromRight (110).reduced (2));
    list.setBounds (area.reduced (12, 0));
}

void MappingsView::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);

    const auto area = getLocalBounds().reduced (24);
    g.setColour (theme.panel);
    g.fillRect (area);
    g.setColour (theme.ink);
    g.drawRect (area.toFloat().reduced (0.75f), 1.5f);

    caption (g, theme, { (float) area.getX() + 14.0f, (float) area.getY() + 12.0f }, "mappings");
    text (g, juce::String ((int) controller.mappings().size()) + " MAPPED",
          { (float) area.getX() + 96.0f, (float) area.getY() + 12.0f, 300.0f, 15.0f },
          theme.ink2, 10.0f, juce::Justification::centredLeft, true, 1.0f);

    const auto listArea = list.getBounds();

    if (listArea.isEmpty())
        return;

    const int width = listArea.getWidth();
    const int colSource = width * 30 / 100;
    const int colAction = width * 20 / 100;
    const int colTarget = width * 30 / 100;

    const float captionY = (float) listArea.getY() - 14.0f;

    text (g, "SOURCE", { (float) listArea.getX() + 8.0f, captionY, (float) colSource - 12.0f, 12.0f },
          theme.ink2, 8.5f, juce::Justification::centredLeft, true, 1.2f);
    text (g, "ACTION", { (float) (listArea.getX() + colSource), captionY, (float) colAction, 12.0f },
          theme.ink2, 8.5f, juce::Justification::centredLeft, true, 1.2f);
    text (g, "TARGET", { (float) (listArea.getX() + colSource + colAction), captionY, (float) colTarget, 12.0f },
          theme.ink2, 8.5f, juce::Justification::centredLeft, true, 1.2f);
    text (g, "RANGE", { (float) (listArea.getX() + colSource + colAction + colTarget), captionY,
                        (float) (width - colSource - colAction - colTarget), 12.0f },
          theme.ink2, 8.5f, juce::Justification::centredLeft, true, 1.2f);

    if (controller.mappings().empty())
        text (g, "NOTHING MAPPED YET - RIGHT-CLICK A KNOB OR A FOOTSWITCH AND CHOOSE LEARN",
              listArea.toFloat().reduced (10.0f), theme.dim, 11.0f, juce::Justification::centred, true, 1.1f);
}

void MappingsView::showRowMenu (int row)
{
    if (row < 0 || row >= (int) controller.mappings().size())
        return;

    const auto mapping = controller.mappings()[(size_t) row];

    juce::PopupMenu actions;
    actions.addItem (kActionToggle, "Toggle", true, mapping.action == ActionType::Toggle);
    actions.addItem (kActionMomentary, "Momentary", true, mapping.action == ActionType::Momentary);
    actions.addItem (kActionThreshold, "Threshold (CC 64)", true, mapping.action == ActionType::CCThreshold);
    actions.addItem (kActionValue, "Value", true, mapping.action == ActionType::SetValue);

    juce::PopupMenu menu;
    menu.addSectionHeader (controller.describeSource (mapping.source));
    menu.addSubMenu ("Change action", actions);
    menu.addItem (kEditRange, "Edit range...");
    menu.addSeparator();
    menu.addItem (kDelete, "Delete");

    juce::Component::SafePointer<MappingsView> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list),
                        [safe, row, mapping] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        switch (result)
        {
            case kActionToggle:    safe->controller.setMappingAction (row, ActionType::Toggle);      break;
            case kActionMomentary: safe->controller.setMappingAction (row, ActionType::Momentary);   break;
            case kActionThreshold: safe->controller.setMappingAction (row, ActionType::CCThreshold); break;
            case kActionValue:     safe->controller.setMappingAction (row, ActionType::SetValue);    break;
            case kDelete:          safe->controller.removeMapping (row);                             break;
            case kEditRange:       safe->promptForRange (row, mapping);                              break;
            default: break;
        }
    });
}

void MappingsView::promptForRange (int row, const Mapping& mapping)
{
    auto* window = new juce::AlertWindow ("Mapping range", "The lowest and highest value this source writes:",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("min", juce::String (mapping.minValue, 2));
    window->addTextEditor ("max", juce::String (mapping.maxValue, 2));
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MappingsView> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, row] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        safe->controller.setMappingRange (row,
                                          window->getTextEditorContents ("min").getFloatValue(),
                                          window->getTextEditorContents ("max").getFloatValue());
    }), true);
}
} // namespace pb
