#pragma once
// Card 8.5: the board's window content, freed from the audio device. This was MainContent in MainWindow.cpp, a
// private class that took AudioEngine&; the app window and the plugin editor need the same board, the same preset
// menu and the same learn flow, and only the device facts differ. Everything the device used to supply arrives
// through HostServices now.
//
// What it still owns: the whole board UI (slots, macro strip, meters, editor panel, overlays) and the working-copy
// autosave, which only happens when the host gives it a working file. What it no longer owns: the registry, the
// router, the board, the processor, and the three control objects (keys, learn, macros). Those outlive this
// component, because a plugin's state and MIDI have to work with no window open (cards 8.2, 8.3).
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Presets/PresetManager.h"
#include "UI/BoardPrototype.h"
#include "UI/GenericEffectEditor.h"
#include "UI/HostServices.h"
#include "UI/IdmLookAndFeel.h"
#include "UI/MacroStrip.h"
#include "UI/MeterBar.h"
#include "UI/PumpStyle.h"
#include "UI/SlotView.h"
#include "UI/TempoField.h"
#include <array>
#include <functional>
#include <memory>

namespace pb
{
class MappingsView;
class WarningBar;

class BoardContent final : public juce::Component, private juce::Timer
{
public:
    BoardContent (HostServices&, BoardProcessor&, ParameterRegistry&, BoardModel&, MidiRouter&,
                  KeyRouter&, LearnController&, MacroBank&);
    ~BoardContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Card 3.2: computer keys are a mapping source, so the same table can drive a knob from the keyboard. In a
    // plugin this only works while the editor holds focus: a host that eats a key wins (card 8.5).
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;

    // Card 8.5: a host restore replaces the whole chain, and the editor panel holds a Slot* into the old one. The
    // plugin calls this as soon as a restore lands, before the panel's own timer can touch a freed slot.
    void repointSlot();

private:
    void timerCallback() override;

    // Rebuilds every pedal's text (it copies each pedal's params(), Strings and all, for up to sixteen slots).
    // The 30 Hz tick passes force = false and then only does the work when something it shows could have changed.
    void syncSlots (bool force = true);
    float slotParamValue (const Slot*, size_t paramIndex, const ParamInfo&) const;

    void moveSlot (int from, int to);
    void selectSlot (int index);
    int slotAtScreen (juce::Point<int> screenPosition) const;
    void updateDropTargets (int from, int target);
    void dropSlot (int from, int target);

    void forgetPedalParams (int index);
    void removeSlotEffect (int index);
    void showSlotMenu (int index);
    void applySlotMenu (int index, int result, const std::vector<juce::String>& typeIds);
    void toggleSlot (int index);

    void updateStatus();
    void showTransient (const juce::String& text);
    void beginNewBoard();
    void startNewBoard();

    void showPresetMenu();
    void applyPresetMenu (int result, const juce::Array<juce::File>& files);
    void promptRenamePreset (const juce::File&);
    void promptDeletePreset (const juce::File&);
    void loadPresetFile (const juce::File&);
    void stepPreset (int direction);
    void quickSave();
    void toggleBoardBypass();
    void updateBypassLight();
    void handleHeaderKey (int key);
    void handleControlEvent (const ControlEvent&);
    void selectPreset (int number);

    void beginLearn (int paramIndex);
    void askForAction();
    juce::String labelForParam (int paramIndex) const;
    void toggleMappings();
    void promptForPresetName();

    bool loadWorkingBoard();
    bool saveWorkingBoard();
    bool savePresetTo (const juce::File&);
    void beginSwap (std::function<void()> swap);
    void updatePresetLabel();
    void showWarnings (const juce::StringArray& messages, const juce::String& title);
    juce::String statusLine() const;

    void toggleSettings();
    void hideSettings();

    // Card 8.5: a question with two answers. An AlertWindow is not a child of this component, so it does not inherit
    // the skin applied here; the plugin would otherwise open stock JUCE grey next to the board.
    void ask (const juce::String& title, const juce::String& message, const juce::String& confirm,
              std::function<void()> onConfirm);

    bool hasWorkingCopy() const { return host.workingBoardFile().getFullPathName().isNotEmpty(); }

    HostServices& host;
    BoardProcessor& processor;
    ParameterRegistry& registry;
    BoardModel& model;
    MidiRouter& midiRouter;
    KeyRouter& keys;                                   // card 3.2: computer keys through the same table
    LearnController& learn;                            // card 3.3: the mapping list and learn handshake
    MacroBank& macros;                                 // card 3.4: eight knobs, each with targets

    style::Theme theme;                                // the window chrome's palette (side column, buttons)
    style::IdmLookAndFeel lookAndFeel;                 // this subtree's skin, never the process's (card 8.5)

    BoardPrototype board;
    MeterBar meters;
    GenericEffectEditor editor;
    MacroStrip macroStrip;
    std::array<std::unique_ptr<SlotView>, 16> slotViews;

    float lastSavedTempo = -1.0f;                      // -1 = the saved tempo has not been adopted yet
    juce::File currentPreset;                          // the named board SAVE and the keys act on
    int boardBypassIndex = ParameterRegistry::kInvalid;
    PresetManager presetManager;                       // card 4.3: boards in ~/Music/pump BOARD/Boards
    std::unique_ptr<WarningBar> warningBar;            // what a load had to drop, dismissable
    std::function<void()> pendingSwap;                 // runs once the bypass fade has finished
    juce::int64 pendingSwapDue = 0;

    juce::TextButton presetsButton { "PRESETS" };
    juce::TextButton newButton { "NEW BOARD" };
    juce::TextButton settingsButton { "SETTINGS" };
    juce::TextButton mappingsButton { "MAPPINGS" };

    std::unique_ptr<juce::Component> settingsView;     // the host's overlay, if it has one
    std::unique_ptr<MappingsView> mappingsView;
    std::unique_ptr<TempoField> tempoField;

    static constexpr int kRemoveItem = 1001;
    static constexpr int kLearnSwitchItem = 1002;
    static constexpr int kSaveAsItem = 2001;
    static constexpr int kLoadBase = 3000;      // + index
    static constexpr int kRenameBase = 4000;    // + index
    static constexpr int kDeleteBase = 5000;    // + index
    static constexpr int kMapToggle = 3001, kMapMomentary = 3002, kMapThreshold = 3003, kMapValue = 3004;

    juce::String transientStatus;
    juce::int64 transientUntil = 0;

    uint32_t lastSeenCounter = 0;
    juce::int64 lastChangeMs = 0;
    bool boardUnsaved = false;

    double lastRate = 0.0;
    int lastBlock = 0;
    int ticks = 0;
    int paintedCount = 0;
    bool syncValid = false;                 // syncSlots() has run at least once
    uint32_t lastSyncCounter = 0;           // registry.changeCounter() at the last syncSlots()
    uint64_t lastSyncSignature = 0;         // identity of the chain at the last syncSlots()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardContent)
};
} // namespace pb
