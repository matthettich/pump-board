#pragma once
// Card 1.5: one knob per ParamInfo for whichever pedal is selected. Double-click resets to the default.
// All values live in ParameterRegistry; this component only reads and writes them.
#include "Control/ParameterRegistry.h"
#include "Engine/Slot.h"
#include "UI/PumpDialLookAndFeel.h"
#include "UI/PumpStyle.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace pb
{
// One param's value as it should read on screen, the same way wherever it appears: the knob's own
// readout, and the value silk-screened on the pedal body.
// Snaps interval-snapping dust (a 0.01 step from -100 lands on 2e-15, not 0) and picks sensible decimals.
juce::String formatParamValue (const ParamInfo&, float value);

class GenericEffectEditor final : public juce::Component, private juce::Timer
{
public:
    explicit GenericEffectEditor (ParameterRegistry&);
    ~GenericEffectEditor() override;

    void setSlot (Slot* slot);                 // nullptr shows the empty panel
    Slot* getSlot() const noexcept { return current; }
    int getNumControls() const noexcept { return (int) controls.size(); }

    // Card 3.3: the control waiting for a source is outlined, and Learn in its menu arms it.
    void setLearnTarget (int paramIndex);
    std::function<void (int paramIndex)> onLearnRequested;

    // Card 3.4: while a macro waits for a target, the next control click is reported instead of acting.
    void setPickTargetMode (bool shouldPick);
    std::function<void (int paramIndex)> onControlPicked;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        ParamInfo info;
        int registryIndex = ParameterRegistry::kInvalid;
        std::unique_ptr<juce::Slider> dial;
        std::unique_ptr<juce::TextButton> toggle;
    };

    void build();
    void timerCallback() override;
    void showParamMenu (int registryIndex, const ParamInfo&, juce::Component& anchor);
    bool pickTarget (int registryIndex);

    ParameterRegistry& registry;
    Slot* current = nullptr;
    int learnTarget = ParameterRegistry::kInvalid;
    bool picking = false;
    style::Theme theme;
    std::unique_ptr<juce::LookAndFeel> dialLookAndFeel;   // declared before the controls it dresses
    std::vector<Control> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericEffectEditor)
};
} // namespace pb
