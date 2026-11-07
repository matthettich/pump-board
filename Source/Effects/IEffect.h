#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace pb
{
struct ParamInfo
{
    // A constructor rather than a bare aggregate, so the 300-odd existing `{ id, name, min, max, default, unit,
    // toggle, skew }` rows keep working without naming `choices` and without -Wmissing-field-initializers. Every
    // field has a default; the adapters pass what they mean.
    ParamInfo() = default;

    ParamInfo (juce::String idIn, juce::String nameIn, float minIn, float maxIn, float defaultIn,
               juce::String unitIn = {}, bool toggleIn = false, float skewIn = 0.0f,
               std::vector<juce::String> choicesIn = {})
        : id (std::move (idIn)), name (std::move (nameIn)), minValue (minIn), maxValue (maxIn),
          defaultValue (defaultIn), unit (std::move (unitIn)), isToggle (toggleIn), skewCentre (skewIn),
          choices (std::move (choicesIn))
    {
    }

    juce::String id;        // stable, lowercase, e.g. "mix", "time"
    juce::String name;      // shown in the UI, e.g. "Mix"
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.5f;
    juce::String unit;      // "dB", "ms", "%", "" ...
    bool isToggle = false;  // 0 or 1 only
    float skewCentre = 0.0f; // 0 = linear; otherwise the value shown at the knob's middle

    // Optional names for an integer switch, in value order starting at minValue. Empty means numbers, which is
    // what every adapter did before card 5.23: pump's own modes are named ("Short tank", "1/4D", "Triode") and
    // the board showed "2.00" instead. Added to this fixed header at the owner's request; see NOTES.md.
    std::vector<juce::String> choices;

    int choiceCount() const noexcept { return (int) choices.size(); }

    // The label for a value, snapped to the nearest choice. Empty when there are no choices, or the value lands
    // outside them — callers fall back to the number, so a stale value can never print a wrong name.
    juce::String choiceLabel (float value) const
    {
        if (choices.empty() || ! std::isfinite (value))
            return {};

        const int index = (int) std::lround (value - minValue);

        return (index >= 0 && index < (int) choices.size()) ? choices[(size_t) index] : juce::String();
    }
};

// Every effect a slot or a custom pedal can hold.
// Construction, params(), prepare(), reset() and createEditor() run on the message thread.
// setParam() and process() run on the audio thread and must be realtime safe.
class IEffect
{
public:
    virtual ~IEffect() = default;

    virtual juce::String typeId() const = 0;        // e.g. "pump.spring"
    virtual juce::String displayName() const = 0;   // e.g. "pump SPRING"
    virtual std::vector<ParamInfo> params() const = 0; // fixed order; index = param index

    // Allocate everything here. Called before the effect is published to the audio thread.
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    // AUDIO THREAD. Called once per block per param with the target value.
    // The effect smooths internally.
    virtual void setParam (int index, float value) noexcept = 0;

    // AUDIO THREAD. In-place, stereo. buffer.getNumSamples() <= maxBlockSize.
    virtual void process (juce::AudioBuffer<float>& buffer) noexcept = 0;

    virtual int latencySamples() const noexcept { return 0; }
    virtual double tailSeconds() const noexcept { return 0.0; }

    // Optional custom editor (e.g. pump's own UI). nullptr = generic knobs.
    virtual std::unique_ptr<juce::Component> createEditor() { return nullptr; }
};

using EffectPtr = std::unique_ptr<IEffect>;
} // namespace pb
