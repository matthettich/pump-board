#pragma once
// Card 3.3: the learn state machine and the mapping list. It lives in Control rather than UI (a deviation
// from the card, logged in docs/NOTES.md) because the test target links PumpBoardCore only: a controller in
// Source/UI could not be tested, and the card's own checks are exactly about learning from a note and a key.
// It touches no GUI class, so the suite can drive it end to end.
#include "Control/KeyRouter.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include <functional>
#include <vector>

namespace pb
{
class LearnController
{
public:
    LearnController (ParameterRegistry&, MidiRouter&, KeyRouter&);
    ~LearnController();                  // unhooks itself from the key router

    // ---- learn ----
    // Arms both sources: the next MIDI message or key becomes a mapping's source.
    void beginLearn (int paramIndex);
    void cancelLearn();
    bool isLearning() const noexcept { return learning; }
    int learningParamIndex() const noexcept { return armedParamIndex; }

    // The source arrives on the message thread: from the FIFO drain, or from KeyRouter's callback.
    void sourceCaptured (const MappingSource&);
    bool hasPendingSource() const noexcept { return pending; }
    MappingSource pendingSource() const noexcept { return pendingSourceValue; }
    int pendingParamIndex() const noexcept { return pendingParam; }
    ActionType defaultActionFor (int paramIndex) const;

    // The user answered the action popup.
    void commitPending (ActionType action);
    void discardPending();

    // ---- the list ----
    const std::vector<Mapping>& mappings() const noexcept { return list; }
    void removeMapping (int index);
    void setMappingAction (int index, ActionType);
    void setMappingRange (int index, float minValue, float maxValue);
    void clearAll();
    // Card 4.2: replace the whole table in one go, with one publish. Loading a board must not leave the
    // router holding half of the old table.
    void setMappings (std::vector<Mapping> newList);

    // For the views. A target's own name is the fallback when the window has no better label for it.
    juce::String describeSource (const MappingSource&) const;
    juce::String describeAction (ActionType) const;
    juce::String describeTarget (int paramIndex) const;

    std::function<juce::String (int paramIndex)> targetLabel;
    std::function<void()> onPendingSource;    // the window asks which action
    std::function<void()> onChange;           // list or armed state moved; views repaint

private:
    void publishTable();
    void notify() const;

    ParameterRegistry& registry;
    MidiRouter& midi;
    KeyRouter& keys;

    std::vector<Mapping> list;

    bool learning = false;
    int armedParamIndex = ParameterRegistry::kInvalid;

    bool pending = false;
    MappingSource pendingSourceValue;
    int pendingParam = ParameterRegistry::kInvalid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnController)
};
} // namespace pb
