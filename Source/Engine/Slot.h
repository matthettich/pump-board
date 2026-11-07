#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include "Effects/IEffect.h"
#include "Control/ParameterRegistry.h"
#include <memory>
#include <vector>

namespace pb
{
// One pedal position. Owns its effect. Survives reordering (same object, new position).
// Construction, setEffect(), prepare(): message thread. process(): audio thread.
class Slot
{
public:
    Slot (juce::String uid, ParameterRegistry& registry);
    ~Slot();                                  // unregisters its params

    const juce::String& getUid() const noexcept { return uid; }

    // Message thread, BEFORE the slot is in a published snapshot.
    // Registers "slot/<uid>/<paramId>" for each effect param, plus "slot/<uid>/on".
    void setEffect (EffectPtr newEffect);

    // Message thread. A fresh Slot that takes over this one's uid and "on" switch (same registry index, so a
    // mapping or macro aimed at the switch keeps working). BoardModel publishes it in this slot's place instead
    // of calling setEffect()/prepare() on a slot the audio thread may be running (review, Nov 2026: replace,
    // clear and a device change used to free the old pedal mid-block). This slot keeps its pedal until the last
    // snapshot holding it is retired; its destructor then leaves the switch alone.
    std::shared_ptr<Slot> makeSuccessor();
    IEffect* getEffect() const noexcept { return effect.get(); }

    void prepare (double sampleRate, int maxBlockSize);

    // AUDIO THREAD. In place, stereo.
    // 1. reads every param target from the registry and calls effect->setParam()
    // 2. copies the dry signal into dryBuffer (preallocated)
    // 3. runs the effect
    // 4. crossfades dry/wet using the "on" param with a 10 ms equal-power ramp
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    int onParamIndex() const noexcept { return onIndex; }
    const std::vector<int>& paramIndices() const noexcept { return indices; }

private:
    struct AdoptSwitch {};
    Slot (const Slot& previous, AdoptSwitch);   // makeSuccessor() only

    juce::String uid;
    ParameterRegistry& registry;
    EffectPtr effect;
    std::vector<int> indices;                 // registry index per effect param (message thread builds it)
    int onIndex = ParameterRegistry::kInvalid;
    bool ownsRegistrations = true;            // message thread only: false once a successor took the switch
    int preparedBlock = 0;                    // process() refuses bigger blocks than the pedal was prepared for
    // Card 5.4: the dry copy is a delay ring, so the on/off crossfade compares dry and wet at the same point in
    // time even when the effect reports latency. 8192 frames is 170 ms at 48k — more than the deepest pedal.
    // (A ring needs a position, which is why this scaffold header gained one int; see NOTES.md, card 5.4.)
    static constexpr int kMaxDelaySamples = 8192;
    juce::AudioBuffer<float> dryBuffer;       // sized in prepare(): one block plus the delay ring
    int dryWritePos = 0;                      // audio thread only
    juce::SmoothedValue<float> wetGain;       // 0 = bypassed, 1 = on
};
} // namespace pb
