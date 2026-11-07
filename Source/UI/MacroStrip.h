#pragma once
// Card 3.4: eight real macro knobs where the painted strip used to be. Each is a registry param
// ("macro/<n>") so MIDI and keys can reach it; right-click names it, or arms a target to be picked
// from the pedal editor, with its range and curve set in a small popup.
#include "Control/MacroBank.h"
#include "Control/ParameterRegistry.h"
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace pb
{
class MacroStrip final : public juce::Component, private juce::Timer
{
public:
    MacroStrip (MacroBank&, ParameterRegistry&, std::function<juce::String (int paramIndex)> labelForParam);

    void paint (juce::Graphics&) override;
    void resized() override;

    // The pedal editor reports the control the user clicked while a macro was waiting for one.
    void addPickedTarget (int paramIndex);
    void cancelPick() { pendingMacro = -1; repaint(); }      // Esc, or the pick abandoned another way

    std::function<void (int macro)> onPickRequested;          // the window arms the editor
    std::function<void (const juce::String&)> onMessage;     // the status line

private:
    void timerCallback() override;
    void showMenu (int macro);
    void renameMacro (int macro);
    void promptForTarget (int macro, int paramIndex);
    juce::Rectangle<float> cellBounds (int macro) const;

    MacroBank& bank;
    ParameterRegistry& registry;
    std::function<juce::String (int paramIndex)> labelForParam;
    style::Theme theme;
    std::unique_ptr<juce::LookAndFeel> dialLookAndFeel;       // before the dials it dresses
    std::array<std::unique_ptr<juce::Slider>, MacroBank::kNumMacros> dials;
    int pendingMacro = -1;                                   // waiting for a control to be picked

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroStrip)
};
} // namespace pb
