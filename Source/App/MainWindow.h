#pragma once
// Card 0.2: the app window. Board on the left, meters and trims on the right, settings over the top.
// Card 2.2: it also drives the board model's garbage collection and mirrors real slot state.
#include "Engine/AudioEngine.h"
#include "Engine/BoardProcessor.h"
#include "UI/BoardPrototype.h"
#include "UI/MeterBar.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace pb
{
class BoardModel;
class SettingsView;
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
    void rebuildContent();

    AudioEngine& engine;
    BoardProcessor& processor;
    ParameterRegistry& registry;
    BoardModel& model;
    MidiRouter& router;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
} // namespace pb
