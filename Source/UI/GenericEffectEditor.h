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

    // Dense pedals are split into pages (at most kMaxPerPage controls each) so every dial stays a readable size.
    static constexpr int kMaxPerPage = 20;
    int getNumPages() const noexcept { return juce::jmax (1, (int) pages.size()); }
    int getPage() const noexcept { return page; }
    void setPage (int newPage);                // clamped; shows only that page's controls

    // Card 3.3: the control waiting for a source is outlined, and Learn in its menu arms it.
    void setLearnTarget (int paramIndex);
    std::function<void (int paramIndex)> onLearnRequested;

    // Card 3.4: while a macro waits for a target, the next control click is reported instead of acting.
    void setPickTargetMode (bool shouldPick);
    bool isPickingTarget() const noexcept { return picking; }
    std::function<void (int paramIndex)> onControlPicked;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Control
    {
        ParamInfo info;
        int registryIndex = ParameterRegistry::kInvalid;
        std::unique_ptr<juce::Slider> dial;
        std::unique_ptr<juce::TextButton> toggle;
        float shownValue = 0.0f;               // the registry value as last painted (open item 4)
        bool shownValid = false;
    };

    struct Page
    {
        int begin = 0, end = 0;                // a contiguous range of `controls`
        juce::String label;                    // the id-prefix group(s), e.g. "BEAST", may be empty
    };

    void build();
    void paginate();
    juce::Rectangle<int> pageKeyBounds (int pageIndex) const;
    int pageKeyAt (juce::Point<int>) const;
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
    std::vector<Page> pages;
    int page = 0;
    Slot* builtFor = nullptr;                  // which slot / effect the controls were built for, so a re-sync
    const void* builtEffect = nullptr;         // of the same pedal keeps the page

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericEffectEditor)
};
} // namespace pb
