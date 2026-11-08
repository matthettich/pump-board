#pragma once
// Card 8.2: the non-UI half of the plugin — everything that has to outlive the editor. A host prepares and runs a
// plugin it has never shown, so the registry, the routers, the board and the processor that plays it cannot live in
// a window. The app keeps its own copies in its window, where a window always exists; this is the plugin's.
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include <functional>

namespace pb
{
class PluginSession final
{
public:
    PluginSession();
    ~PluginSession();

    // Message thread, from the host's prepareToPlay. Every slot is re-prepared for the new rate and block; the audio
    // thread is not running, so this does not wait for it (see BoardModel::prepareAllForHost).
    void prepare (double sampleRate, int maximumBlockSize);

    // Message thread, ~30 Hz while the plugin is loaded: frees what the audio thread can no longer be using. It runs
    // from the processor's own timer, which is why it does not need an editor to exist.
    void collectGarbage();

    // Card 8.5: while an editor is open its own timer drains the router's events (learn results, program changes);
    // with no editor there is nobody to hear them, and an undrained FIFO would fill up for the life of the plugin.
    void setEditorAttached (bool attached);

    // The board the plugin opens on: the same three pedals the app starts with. Card 8.3 replaces this with the
    // host's saved state.
    void loadDefaultBoard();

    // ---- Card 8.3: host state ----
    // Message thread. One JSON object: this plugin's tag, the state version, the board file's own document kept as
    // a string, and the tempo the board was playing at. Storing the .board document rather than a restructured copy
    // is what keeps a host project and a saved preset from drifting apart.
    juce::var captureState() const;

    // Message thread. Applies through the same path a preset load uses. False, with the board untouched, for
    // anything it does not recognise: not an object, another product's state, a version this build does not write,
    // truncated JSON, or a board document that will not parse.
    bool restoreState (const juce::var& state);

    // Card 8.3: why the last restore was refused. A host can only see the return value, which says nothing, and
    // "the tempo is wrong" is a poor description of a state that was dropped. Empty after a restore that worked.
    const juce::String& getLastRestoreError() const noexcept { return lastRestoreError; }

    // Card 8.5: set by the editor. A restore frees the slots underneath it, so it has to re-point at the new chain
    // before its panel's timer can touch a freed one. Called on the message thread, only after a restore succeeded.
    std::function<void()> onBoardReplaced;

    // The fade around a restore, for the processor: the board's own bypass smoother is the fade.
    bool isBoardBypassed() const;
    void setBoardBypassed (bool bypassed);

    ParameterRegistry& getRegistry() noexcept { return registry; }

    // Card 8.6: cached at construction, so the audio thread can write the host's tempo without a string compare per
    // block. `board/tempo` is registered by the board's processor, which the session owns, so it exists by now.
    int getTempoParamIndex() const noexcept { return tempoIndex; }
    MidiRouter& getMidiRouter() noexcept { return midiRouter; }
    KeyRouter& getKeyRouter() noexcept { return keys; }
    LearnController& getLearn() noexcept { return learn; }
    MacroBank& getMacros() noexcept { return macros; }
    BoardProcessor& getProcessor() noexcept { return processor; }
    BoardModel& getModel() noexcept { return model; }

private:
    // Declaration order is construction order, and the reverse is destruction order: the board goes before the
    // processor that plays it, and the processor before the registry its parameters live in.
    ParameterRegistry registry;
    MidiRouter midiRouter;
    KeyRouter keys;
    LearnController learn;
    MacroBank macros;
    BoardProcessor processor;
    BoardModel model;
    int tempoIndex = ParameterRegistry::kInvalid;
    bool editorAttached = false;
    juce::String lastRestoreError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSession)
};
} // namespace pb
