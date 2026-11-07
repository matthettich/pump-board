#pragma once
// Card 8.2: the non-UI half of the plugin — everything that has to outlive the editor. A host prepares and runs a
// plugin it has never shown, so the registry, the routers, the board and the processor that plays it cannot live in
// a window. The app keeps its own copies inside MainContent, where a window always exists; this is the plugin's.
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"

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

    // The board the plugin opens on: the same three pedals the app starts with. Card 8.3 replaces this with the
    // host's saved state.
    void loadDefaultBoard();

    ParameterRegistry& getRegistry() noexcept { return registry; }
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSession)
};
} // namespace pb
