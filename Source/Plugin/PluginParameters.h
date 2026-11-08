#pragma once
// Card 8.4: what a host can automate. Every parameter reads and writes one ParameterRegistry index, so the value has
// a single home and a knob, a macro, a mapped footswitch and a host lane all write to the same place.
//
// The set is the one chosen before card 8.1: macros 0..7, board/bypass, board/inputTrim, board/outputTrim and the
// sixteen slot switches. Per-pedal knobs stay in the UI and in host state, not as automatable parameters: their
// registry indices are minted per slot uid and move with every insert, swap and load.
//
// Automation follows the slot, not the pedal. `slot/3/on` is a position in the chain, and it keeps naming position 3
// after a pedal is inserted before it, exactly as a fixed-lane mixer's channel 3 does. An id that travelled with the
// pedal would change the parameter list at runtime, which hosts do not accept, and would leave a saved automation
// lane pointing at a pedal that may no longer exist. The consequence, stated for the docs: inserting a pedal shifts
// which pedal an existing lane drives; the pedal's own settings move with the pedal, its lane does not.
#include "Control/MacroBank.h"
#include "Control/ParameterRegistry.h"
#include "Engine/BoardModel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

namespace pb
{
class PluginParameter final : public juce::AudioProcessorParameterWithID,
                              private juce::AudioProcessorParameter::Listener
{
public:
    enum class Kind { continuous, toggle };

    PluginParameter (ParameterRegistry&, juce::String paramId, juce::String paramName, Kind,
                     float minValue, float maxValue, float defaultValue,
                     int registryIndex = ParameterRegistry::kInvalid, juce::String suffix = {});
    ~PluginParameter() override;

    // Any thread. The registry is the truth: a host that asks, and a host that writes, both go through it.
    float getValue() const override;
    void setValue (float newValue) override;          // AUDIO THREAD in some hosts: registry writes are lock-free
    float getDefaultValue() const override;
    int getNumSteps() const override;
    juce::String getText (float normalisedValue, int maximumStringLength) const override;
    float getValueForText (const juce::String& text) const override;   // the inverse, for a host that types a value
    juce::String getName (int maximumStringLength) const override;

    // A gesture the host started means it is writing this parameter itself, so the poll below must not answer it.
    // beginChangeGesture()/endChangeGesture() are not virtual in this JUCE version — the host calls the base class's
    // own — so the parameter registers itself as a listener instead (see parameterGestureChanged below).

    // Message thread. The index a parameter drives, or kInvalid while there is nothing there: an empty slot has no
    // switch at all, and a stale index would be a footswitch quietly driving whatever pedal took the slot next.
    void setRegistryIndex (int newIndex) noexcept;
    int getRegistryIndex() const noexcept { return index.load (std::memory_order_relaxed); }

    // Message thread only, and only for a macro whose name the user changed. Hosts read names from this thread.
    void setName (const juce::String& newName);

    // Message thread, ~30 Hz: a change that did not come through the host (a knob, a macro, a mapped footswitch) is
    // the one thing a host cannot see for itself, so it is told exactly once. The comparison is against the last
    // value this parameter itself pushed, not against a change counter: a counter cannot tell the two directions
    // apart, and this can.
    void pollRegistry();

    // Message thread: adopt the current values without telling the host, for a restore it just asked for.
    void resync() noexcept;

private:
    float normalised (float actual) const noexcept;
    float actualValue (float normalisedValue) const noexcept;

    // AudioProcessorParameter::Listener, for this parameter's own gestures only. A value notification it sends comes
    // back here too, and is ignored: the parameter already knows what it pushed.
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool isStarting) override;

    ParameterRegistry& registry;
    const Kind kind;
    const float minValue, maxValue, defaultValue;
    const juce::String suffix;

    juce::String currentName;
    std::atomic<int> index;
    std::atomic<float> lastPushed;
    std::atomic<bool> hostGesture { false };
};

class PluginParameters
{
public:
    static constexpr int kNumSlots = 16;

    // Builds the whole set from the registry that is already populated (the session's processor registers the board
    // parameters, MacroBank registers macro/0..7) and from the chain as it stands.
    PluginParameters (ParameterRegistry&, MacroBank&, BoardModel&);

    // The processor takes ownership of the parameters, as addParameter() expects.
    void addTo (juce::AudioProcessor&);

    void poll();
    void resync() noexcept;

    // Message thread. Re-points the sixteen slot switches at their slots, and reports whether anything a host draws
    // changed (a pedal arrived, left or was swapped), so the processor can ask the host to redraw its generic editor.
    bool refreshChain();

    PluginParameter* find (const juce::String& paramId) const;

private:
    ParameterRegistry& registry;
    MacroBank& macros;
    BoardModel& model;

    std::vector<PluginParameter*> parameters;          // owned by the AudioProcessor once addTo() has run
    std::array<PluginParameter*, kNumSlots> slotOn {};
    std::array<PluginParameter*, MacroBank::kNumMacros> macroParams {};      // first eight, in macro order
    uint64_t lastSignature = 0;
    bool chainValid = false;
};
} // namespace pb
