#pragma once
// Card 0.2: the app window. Board on the left, meters and trims on the right, settings over the top.
// Card 8.5: the window is now only a window. What used to be its content — the board, the side column, the status
// line, the overlays — is BoardContent, shared with the plugin editor. What is left here is the frame: the device
// the app opened, the look-and-feel the process runs in, and the objects that must outlive a skin change.
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/ParameterRegistry.h"
#include "Engine/AudioEngine.h"
#include "Engine/BoardProcessor.h"
#include "UI/HostServices.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace pb
{
class BoardModel;
class MidiRouter;

class MainWindow final : public juce::DocumentWindow
{
public:
    // Card 3.2: the router comes in too, so computer keys use the same mapping table as MIDI.
    MainWindow (AudioEngine&, BoardProcessor&, ParameterRegistry&, BoardModel&, MidiRouter&);
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    // Card 5.5: the content is rebuilt when the skin changes, so the window keeps the engines it was given.
    void buildContent();

    AudioEngine& engine;
    BoardProcessor& processor;
    ParameterRegistry& registry;
    BoardModel& model;
    MidiRouter& router;

    // Card 8.5: these three used to be MainContent's own. The content is rebuilt on a skin change, so holding them
    // here is what stops a reskin from resetting the macros and dropping the mapping table with it.
    KeyRouter keys;
    LearnController learn;
    MacroBank macros;

    std::unique_ptr<HostServices> host;   // the app's answers to what the content asks the device

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
} // namespace pb
