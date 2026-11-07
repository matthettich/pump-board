#pragma once
// Card 3.2: computer keys as a first-class mapping source, through the same immutable table MIDI uses.
// It tracks which keys are held (so a Momentary mapping holds a switch only while its key is down),
// swallows auto-repeat, and hands preset actions straight back to the message thread.
#include "Control/MidiRouter.h"
#include <array>
#include <functional>

namespace pb
{
class KeyRouter
{
public:
    explicit KeyRouter (MidiRouter& router);

    // Message thread. Returns true when the key did something the window should not pass on.
    bool keyDown (int keyCode);
    bool keyUp (int keyCode);

    // juce::Component::keyStateChanged() does not say which key changed, so the window asks whether each
    // held key is still down; the ones that are gone are released (which is what holds Momentary honest).
    template <typename Fn> void pollReleasedKeys (Fn&& isStillDown);

    void beginLearn();
    void cancelLearn();
    bool isLearning() const noexcept { return learning; }

    // Message thread only. Fired when a key is taken as a mapping's source; it does not also act.
    std::function<void (int keyCode)> onLearnCaptured;
    // Fired when a mapped key asks for a preset. Never queued through the audio thread's FIFO.
    std::function<void (const ControlEvent&)> onPresetAction;

private:
    bool isHeld (int keyCode) const noexcept;
    void addHeld (int keyCode) noexcept;
    void removeHeld (int keyCode) noexcept;

    MidiRouter& router;
    bool learning = false;

    static constexpr int kMaxHeldKeys = 16;
    std::array<int, kMaxHeldKeys> held {};
    int numHeld = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyRouter)
};

template <typename Fn>
void KeyRouter::pollReleasedKeys (Fn&& isStillDown)
{
    // Backwards, because removeHeld() swap-removes: every key above i has already been checked.
    for (int i = numHeld - 1; i >= 0; --i)
        if (! isStillDown (held[(size_t) i]))
            keyUp (held[(size_t) i]);
}
} // namespace pb
