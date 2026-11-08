#pragma once
// Card 2.2: the board as the message thread sees it. Every change builds a ChainSnapshot and hands it to
// BoardProcessor, which swaps it in for the audio thread.
#include "Effects/EffectRegistry.h"
#include "Engine/ChainSnapshot.h"
#include "Engine/Slot.h"
#include <vector>

namespace pb
{
class BoardProcessor;

// Message thread only.
class BoardModel
{
public:
    BoardModel (BoardProcessor&, EffectRegistry&, ParameterRegistry&);

    int size() const noexcept { return (int) entries.size(); }
    Slot* at (int index) const;
    juce::String categoryAt (int index) const;
    int indexOfUid (const juce::String& uid) const;

    // All of these republish. insert() prepares the new slot with the processor's current rate and block.
    bool insert (int index, const juce::String& typeId);       // false when full or the type is unknown
    bool replace (int index, const juce::String& typeId);      // keeps the uid and the on switch (a successor Slot)
    bool insertEmpty (int index);                              // an empty position, as a loaded board lists it
    // Empties a position: the Slot, its uid and its place stay, the pedal and its params go, so the
    // board does not renumber under the player's feet. False when the position was already empty.
    bool clearSlot (int index);
    void remove (int index);
    bool move (int from, int to);
    void clear();

    // Re-prepares every slot; call after the device changes sample rate or buffer size. Briefly detaches the chain
    // (the board plays dry for a couple of blocks) so no slot is re-prepared under the audio thread.
    void prepareAll();

private:
    void publish();

    struct Entry
    {
        std::shared_ptr<Slot> slot;
        juce::String category;
    };

    BoardProcessor& processor;
    EffectRegistry& effects;
    ParameterRegistry& registry;
    std::vector<Entry> entries;
    int uidCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardModel)
};
} // namespace pb
