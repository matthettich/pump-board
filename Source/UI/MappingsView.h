#pragma once
// Card 3.3: every mapping, with the source it came from, what it does, what it drives and its range.
// Click a row to change its action, edit its range, or delete it.
#include "Control/LearnController.h"
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace pb
{
class MappingsView final : public juce::Component
{
public:
    explicit MappingsView (LearnController&);
    ~MappingsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();

    std::function<void()> onClose;

private:
    class Model;

    void showRowMenu (int row);
    void promptForRange (int row, const Mapping&);

    static constexpr int kActionToggle = 1, kActionMomentary = 2, kActionThreshold = 3,
                         kActionValue = 4, kEditRange = 5, kDelete = 6;

    LearnController& controller;
    style::Theme theme;
    std::unique_ptr<Model> model;        // declared before the list that borrows it
    juce::ListBox list;
    juce::TextButton closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingsView)
};
} // namespace pb
