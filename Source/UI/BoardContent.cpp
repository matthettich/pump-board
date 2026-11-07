#include "UI/BoardContent.h"
#include "Common/AllocationGuard.h"
#include "Common/AppSettings.h"
#include "Effects/EffectRegistry.h"
#include "Engine/LoadMeter.h"
#include "Presets/BoardPreset.h"
#include "Presets/BoardState.h"
#include "UI/MappingsView.h"
#include "UI/WarningBar.h"
#include <algorithm>
#include <cstdint>

namespace pb
{
namespace
{
// The side column: four buttons and the meters, 200 px, as the fixed layout has always had it.
constexpr int kSideWidth = 200;
constexpr int kButtonHeight = 52;

// Up is faster, three pixels to a beat: 20 to 300 fits in one comfortable pull.
constexpr float kPixelsPerBpm = 3.0f;
} // namespace

BoardContent::BoardContent (HostServices& hostIn, BoardProcessor& processorIn, ParameterRegistry& registryIn,
                            BoardModel& modelIn, MidiRouter& routerIn, KeyRouter& keysIn,
                            LearnController& learnIn, MacroBank& macrosIn)
    : host (hostIn),
      processor (processorIn),
      registry (registryIn),
      model (modelIn),
      midiRouter (routerIn),
      keys (keysIn),
      learn (learnIn),
      macros (macrosIn),
      meters (processorIn, registryIn),
      editor (registryIn),
      macroStrip (macrosIn, registryIn, [this] (int paramIndex) { return labelForParam (paramIndex); }),
      presetManager (PresetManager::forApp())
{
    theme.build();   // the window's own chrome, not a pedal's accent

    // Card 8.5: the skin belongs to this subtree. The app also sets a process-wide default in Main.cpp for its own
    // stock widgets; a plugin that did the same would restyle its host, so it never touches the default and every
    // dialog it opens is handed this look-and-feel explicitly (see ask()).
    setLookAndFeel (&lookAndFeel);

    setWantsKeyboardFocus (true);     // an unmapped key still reaches keyPressed, and nothing else wants it

    addAndMakeVisible (board);
    addAndMakeVisible (macroStrip);  // card 3.4: eight real knobs where the painted strip was
    addAndMakeVisible (editor);      // on top of the board, where its painted placeholder used to be
    addAndMakeVisible (meters);
    addAndMakeVisible (presetsButton);
    addAndMakeVisible (settingsButton);
    addAndMakeVisible (mappingsButton);
    addAndMakeVisible (newButton);

    // Card 2.5: sixteen real slot components on the board's slot positions; the painted pedals go away.
    board.setPaintedSlotsVisible (false);
    board.setPaintedMacrosVisible (false);      // card 3.4: the macro strip is real now too

    for (int i = 0; i < 16; ++i)
    {
        auto view = std::make_unique<SlotView> (i);
        view->onToggle = [this] (int slotIndex) { toggleSlot (slotIndex); };
        view->onSelect = [this] (int slotIndex) { selectSlot (slotIndex); };
        view->onMenu = [this] (int slotIndex) { showSlotMenu (slotIndex); };
        view->onRemove = [this] (int slotIndex) { removeSlotEffect (slotIndex); };
        view->onDrag = [this] (int from, juce::Point<int> screen)
        {
            updateDropTargets (from, slotAtScreen (screen));
        };
        view->onDrop = [this] (int from, juce::Point<int> screen)
        {
            dropSlot (from, slotAtScreen (screen));
        };

        board.addAndMakeVisible (*view);
        slotViews[(size_t) i] = std::move (view);
    }

    // The board's tempo lives in the header, where a musician looks for it.
    tempoField = std::make_unique<TempoField> (registry);
    tempoField->setEditable (host.tempoEditable());     // card 8.5: a plugin's comes from the host
    board.addAndMakeVisible (*tempoField);

    for (auto* button : { &settingsButton, &presetsButton, &mappingsButton, &newButton })
    {
        button->setColour (juce::TextButton::buttonColourId, theme.panel2);
        button->setColour (juce::TextButton::textColourOffId, theme.ink);
        button->setColour (juce::TextButton::textColourOnId, theme.ink);
    }

    // A plugin has no device to pick, so it is not offered the panel that picks one.
    settingsButton.setVisible (host.hasSettingsView());

    settingsButton.onClick = [this] { toggleSettings(); };

    presetsButton.onClick = [this] { showPresetMenu(); };

    mappingsButton.onClick = [this] { toggleMappings(); };

    newButton.onClick = [this] { beginNewBoard(); };

    board.onSlotToggle = [this] (int index) { toggleSlot (index); };
    board.onSlotSelected = [this] (int index) { editor.setSlot (model.at (index)); };
    board.onSlotMoved = [this] (int from, int to) { moveSlot (from, to); };
    board.onSlotMenu = [this] (int index) { showSlotMenu (index); };
    board.onHeaderKey = [this] (int key) { handleHeaderKey (key); };
    keys.onPresetAction = [this] (const ControlEvent& event) { handleControlEvent (event); };

    // Card 3.3: Learn. A key or a MIDI message becomes the source; both stop waiting when either wins.
    editor.onLearnRequested = [this] (int paramIndex) { beginLearn (paramIndex); };

    learn.onPendingSource = [this] { askForAction(); };
    learn.targetLabel = [this] (int paramIndex) { return labelForParam (paramIndex); };
    learn.onChange = [this]
    {
        if (mappingsView != nullptr)
            mappingsView->refresh();
    };

    // Card 3.4: a macro's "Add target" arms the editor; the next control clicked becomes the target.
    macroStrip.onPickRequested = [this] (int macro)
    {
        editor.setPickTargetMode (true);
        showTransient ("CLICK A KNOB TO ADD IT TO " + macros.getName (macro));
    };

    macroStrip.onMessage = [this] (const juce::String& text) { showTransient (text); };
    editor.onControlPicked = [this] (int paramIndex) { macroStrip.addPickedTarget (paramIndex); };

    boardBypassIndex = registry.find ("board/bypass");
    board.setPaintedEditorVisible (false);

    // Card 4.3: the board the app was last on reopens — slots, macros and mappings. A board left by the old
    // auto-save (board.xml, slots only) is imported once, so an upgrade does not lose the pedals.
    // Card 8.5: only where the host keeps a working copy. A plugin's board arrives from the host's state instead,
    // and it must not be overwritten by whatever the standalone happened to leave on disk.
    if (hasWorkingCopy() && ! loadWorkingBoard() && pb::loadBoard (pb::boardFile(), model, registry))
    {
        juce::Logger::writeToLog ("pump BOARD: imported " + juce::String (model.size())
                                  + " pedal(s) from the old board.xml");
        boardUnsaved = true;
        lastChangeMs = juce::Time::currentTimeMillis();
    }

    if (hasWorkingCopy() && model.size() > 0 && currentPreset.getFullPathName().isEmpty())
    {
        // Registering a pedal's params does not bump the change counter, so a board built at startup has
        // never been marked for writing; say so here or the first run would leave nothing behind.
        boardUnsaved = true;
        lastChangeMs = juce::Time::currentTimeMillis();
    }

    lastSeenCounter = registry.changeCounter();

    lastRate = processor.getSampleRateHz();
    lastBlock = processor.getMaxBlock();

    syncSlots();
    updateStatus();
    selectSlot (0);
    updatePresetLabel();
    startTimerHz (30);
}

BoardContent::~BoardContent()
{
    // Card 4.3: the working copy is what the next launch opens, macros and mappings included. A plugin has none.
    if (hasWorkingCopy())
        saveWorkingBoard();

    // Card 8.5: these four callbacks capture `this`, and the controller they live on outlives the window: a plugin's
    // session keeps its learn and its key router with the editor shut. An editor that closes while a learn is armed
    // must not leave them pointing at a dead component.
    learn.onPendingSource = nullptr;
    learn.targetLabel = nullptr;
    learn.onChange = nullptr;
    keys.onPresetAction = nullptr;

    setLookAndFeel (nullptr);        // ours is a member, and the children go with us
}

// The strip behind the side buttons is the one area no panel covers: fill it with the board's paper.
void BoardContent::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
}

void BoardContent::resized()
{
    auto r = getLocalBounds();

    board.setBounds (r.removeFromLeft (juce::jmax (600, r.getWidth() - kSideWidth)));

    auto side = r;
    settingsButton.setBounds (side.removeFromBottom (kButtonHeight).reduced (12, 10));
    presetsButton.setBounds (side.removeFromBottom (kButtonHeight).reduced (12, 10));
    mappingsButton.setBounds (side.removeFromBottom (kButtonHeight).reduced (12, 10));
    newButton.setBounds (side.removeFromBottom (kButtonHeight).reduced (12, 10));   // top of the button column
    meters.setBounds (side);

    for (int i = 0; i < 16; ++i)
        slotViews[(size_t) i]->setBounds (board.getSlotBounds (i).getSmallestIntegerContainer());

    if (tempoField != nullptr)
        tempoField->setBounds (board.tempoBounds().getSmallestIntegerContainer());

    const auto macroArea = board.macroBounds();
    macroStrip.setBounds (board.getX() + (int) macroArea.getX(), board.getY() + (int) macroArea.getY(),
                          (int) macroArea.getWidth(), (int) macroArea.getHeight());

    const auto editorArea = board.editorBounds();
    editor.setBounds (board.getX() + (int) editorArea.getX(), board.getY() + (int) editorArea.getY(),
                      (int) editorArea.getWidth(), (int) editorArea.getHeight());

    if (settingsView != nullptr)
        settingsView->setBounds (getLocalBounds());

    if (mappingsView != nullptr)
        mappingsView->setBounds (getLocalBounds());

    if (warningBar != nullptr)
        warningBar->setBounds (getLocalBounds().removeFromBottom (96).reduced (12, 6));
}

bool BoardContent::keyPressed (const juce::KeyPress& key)
{
    // A board name or a tempo is being typed: those keys belong to the text field, not to the board.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return false;

    // Card 3.3: Esc gets out of learn without touching anything else.
    if (key.getKeyCode() == juce::KeyPress::escapeKey
        && (learn.isLearning() || learn.hasPendingSource()))
    {
        learn.cancelLearn();
        editor.setLearnTarget (ParameterRegistry::kInvalid);
        showTransient ("LEARN CANCELLED");
        return true;
    }

    // Esc also abandons a macro's "Add target": otherwise the next knob clicked, whenever that is, silently
    // opens a target dialog instead of doing what the click meant.
    if (key.getKeyCode() == juce::KeyPress::escapeKey && editor.isPickingTarget())
    {
        editor.setPickTargetMode (false);
        macroStrip.cancelPick();
        showTransient ("ADD TARGET CANCELLED");
        return true;
    }

    return keys.keyDown (key.getKeyCode());
}

bool BoardContent::keyStateChanged (bool)
{
    // The callback does not say which key changed, so ask about the ones we are holding.
    keys.pollReleasedKeys ([] (int keyCode) { return juce::KeyPress::isKeyCurrentlyDown (keyCode); });
    return false;                     // a release is not consumed; there is nothing left to pass on
}

void BoardContent::repointSlot()
{
    // The chain underneath changed without a click: everything that names a slot has to be re-read.
    syncSlots();
    selectSlot (juce::jlimit (0, juce::jmax (0, model.size() - 1), board.selectedSlot()));
    updateBypassLight();
    updatePresetLabel();
}

void BoardContent::timerCallback()
{
    // Card 2.2: snapshots the audio thread can no longer be using are freed here. (In a plugin the processor runs
    // its own timer for this too, which is harmless: the retire counter only ever moves one way.)
    processor.collectGarbage();

    // Card 3.4: a macro that moved (by hand, by MIDI or by a key) writes its targets here.
    macros.update();

    // Card 3.3: mapping tables are published now, so the router retires the ones it replaced, and the
    // two things the audio thread posts (learn captures, preset actions) are handled here.
    midiRouter.collectGarbage();
    midiRouter.drainEvents ([this] (const ControlEvent& event)
    {
        if (event.kind == ControlEvent::Kind::LearnCaptured)
            learn.sourceCaptured (event.source);
        else
            handleControlEvent (event);
    });

    // A device change means new sample rate or block size; the slots are rebuilt rather than re-prepared,
    // so the audio thread never touches a slot that is being reallocated underneath it. A plugin's host owns
    // that moment instead: it calls prepareToPlay, which the processor answers (card 8.2).
    const double rate = processor.getSampleRateHz();
    const int block = processor.getMaxBlock();

    if (host.preparesBoardOnDeviceChange()
        && (std::abs (rate - lastRate) > 1.0e-6 || block != lastBlock))
    {
        lastRate = rate;
        lastBlock = block;
        model.prepareAll();
    }

    syncSlots (false);                  // the per-tick pass: skipped unless a value or the model moved
    updateBypassLight();

    const auto now = juce::Time::currentTimeMillis();

    // Card 4.3: a swap waits for the bypass fade, then writes the new board in and unbypasses.
    if (pendingSwap != nullptr && now >= pendingSwapDue)
    {
        auto swap = std::move (pendingSwap);
        pendingSwap = nullptr;
        swap();

        if (boardBypassIndex != ParameterRegistry::kInvalid)
            registry.set (boardBypassIndex, 0.0f);

        // Unbypassing moves the change counter; that is the load finishing, not an edit, so the board must not
        // come up starred and be written straight back out.
        lastSeenCounter = registry.changeCounter();

        updateBypassLight();
    }

    // Presets: write the board out shortly after it stops changing, so a restart finds it again.
    // Debounced because a knob drag bumps the counter on every move.
    const auto counter = registry.changeCounter();

    if (counter != lastSeenCounter)
    {
        lastSeenCounter = counter;
        lastChangeMs = now;
        boardUnsaved = true;
    }

    if (hasWorkingCopy() && boardUnsaved && now - lastChangeMs > 800)
        if (saveWorkingBoard())
            boardUnsaved = false;

    if (++ticks % 15 == 0)
        updateStatus();
}

void BoardContent::syncSlots (bool force)
{
    const int count = model.size();

    uint64_t signature = UINT64_C (1469598103934665603) ^ (uint64_t) count;       // FNV-1a over the chain's identity

    for (int i = 0; i < count; ++i)
    {
        const auto* slot = model.at (i);
        const auto* effect = slot != nullptr ? slot->getEffect() : nullptr;

        for (const auto pointer : { (uintptr_t) slot, (uintptr_t) effect })
            signature = (signature ^ (uint64_t) pointer) * UINT64_C (1099511628211);
    }

    const auto counterNow = registry.changeCounter();

    if (! force && syncValid && counterNow == lastSyncCounter && signature == lastSyncSignature)
        return;

    syncValid = true;
    lastSyncCounter = counterNow;
    lastSyncSignature = signature;

    for (int i = 0; i < count; ++i)
    {
        if (auto* slot = model.at (i))
        {
            const bool on = slot->onParamIndex() != ParameterRegistry::kInvalid
                            && registry.get (slot->onParamIndex()) >= 0.5f;

            auto* effect = slot->getEffect();
            juce::String lines[2];

            if (effect != nullptr)
            {
                // The two values silk-screened on the pedal: the first two real params, so the body
                // cannot claim something the pedal is not doing.
                const auto params = effect->params();
                int shown = 0;

                for (size_t p = 0; p < params.size() && shown < 2; ++p)
                {
                    if (params[p].isToggle || params[p].id == "model")
                        continue;

                    lines[shown++] = params[p].name.toUpperCase() + " "
                                   + formatParamValue (params[p], slotParamValue (slot, p, params[p]));
                }
            }

            const auto display = effect != nullptr ? effect->displayName() : juce::String ("EMPTY");
            board.setSlotState (i, display, model.categoryAt (i).toUpperCase(), on, lines[0], lines[1]);
            slotViews[(size_t) i]->setSlot (effect != nullptr ? display : juce::String(),
                                            model.categoryAt (i).toUpperCase(), lines[0], lines[1], on);
        }
    }

    // A slot that was removed would otherwise keep its painted name.
    for (int i = count; i < paintedCount; ++i)
    {
        board.setSlotState (i, "", "", true);
        slotViews[(size_t) i]->setSlot ("", "", "", "", true);
    }

    board.setPedalCount (count);      // the header counts what is really there

    paintedCount = count;
}

float BoardContent::slotParamValue (const Slot* slot, size_t paramIndex, const ParamInfo& info) const
{
    const auto& indices = slot->paramIndices();

    if (paramIndex < indices.size() && indices[paramIndex] != ParameterRegistry::kInvalid)
        return registry.get (indices[paramIndex]);

    return info.defaultValue;
}

// Card 2.5: carry a pedal to another position. The model keeps the same slot (and uid), so the pedal keeps
// every value it had; only its place in the chain changes.
void BoardContent::moveSlot (int from, int to)
{
    if (! model.move (from, to))
        return;

    syncSlots();
    selectSlot (to);
}

void BoardContent::selectSlot (int index)
{
    board.setSelectedSlot (index);
    editor.setSlot (model.at (index));

    for (int i = 0; i < 16; ++i)
        slotViews[(size_t) i]->setSelected (i == index);
}

// The slot under a screen position, so a drag can be resolved against the whole board.
int BoardContent::slotAtScreen (juce::Point<int> screenPosition) const
{
    const auto point = board.getLocalPoint (nullptr, screenPosition).toFloat();

    for (int i = 0; i < 16; ++i)
        if (board.getSlotBounds (i).contains (point))
            return i;

    return -1;
}

void BoardContent::updateDropTargets (int from, int target)
{
    for (int i = 0; i < 16; ++i)
        slotViews[(size_t) i]->setDropTarget (i == target && i != from);
}

void BoardContent::dropSlot (int from, int target)
{
    updateDropTargets (from, -1);

    if (from >= 0 && target >= 0 && from != target)
        moveSlot (from, target);
}

// Taking a pedal off empties its position instead of closing the gap. Two reasons: the empty "+" is where the
// next pedal goes, and renumbering the board under the player's feet (and under every macro and mapping that
// names a slot) is a worse surprise than an empty slot.
// A pedal that goes (removed or swapped) takes its params with it, and the registry reuses freed indices: a
// footswitch or macro still aimed at one would quietly start driving whatever pedal lands there next
// (review, Nov 2026). The slot's own on switch survives a swap, so mappings to it are kept.
void BoardContent::forgetPedalParams (int index)
{
    auto* slot = model.at (index);

    if (slot == nullptr)
        return;

    // An armed or pending learn may be aimed at one of the indices about to be freed and reused: cancel it, or the
    // next MIDI message would commit a mapping onto whatever pedal takes that index (CodeRabbit, PR #1).
    if (learn.isLearning() || learn.hasPendingSource())
    {
        learn.cancelLearn();
        editor.setLearnTarget (ParameterRegistry::kInvalid);
    }

    const auto& gone = slot->paramIndices();
    const auto isGone = [&gone] (int paramIndex)
    {
        return paramIndex != ParameterRegistry::kInvalid
            && std::find (gone.begin(), gone.end(), paramIndex) != gone.end();
    };

    for (int i = (int) learn.mappings().size(); --i >= 0;)
        if (isGone (learn.mappings()[(size_t) i].paramIndex))
            learn.removeMapping (i);

    for (int m = 0; m < MacroBank::kNumMacros; ++m)
    {
        auto targets = macros.getTargets (m);
        const auto before = targets.size();
        targets.erase (std::remove_if (targets.begin(), targets.end(),
                                       [&isGone] (const MacroBank::Target& t) { return isGone (t.paramIndex); }),
                       targets.end());

        if (targets.size() != before)
            macros.setTargets (m, std::move (targets));
    }
}

void BoardContent::removeSlotEffect (int index)
{
    auto* slot = model.at (index);

    if (slot == nullptr || slot->getEffect() == nullptr)
        return;

    const auto name = slot->getEffect()->displayName();

    forgetPedalParams (index);

    if (! model.clearSlot (index))
        return;

    syncSlots();
    selectSlot (index);                 // the editor shows the now-empty position rather than a ghost
    updateStatus();

    showTransient ("REMOVED   " + name.toUpperCase());
}

void BoardContent::showSlotMenu (int index)
{
    const auto descriptors = EffectRegistry::instance().list();

    juce::PopupMenu menu;
    std::vector<juce::String> typeIds;
    std::vector<juce::String> categories;

    for (const auto& descriptor : descriptors)
        if (std::find (categories.begin(), categories.end(), descriptor.category) == categories.end())
            categories.push_back (descriptor.category);

    for (const auto& category : categories)
    {
        juce::PopupMenu submenu;

        for (const auto& descriptor : descriptors)
            if (descriptor.category == category)
            {
                typeIds.push_back (descriptor.typeId);
                submenu.addItem ((int) typeIds.size(), descriptor.displayName);
            }

        menu.addSubMenu (category.toUpperCase(), submenu);
    }

    if (model.at (index) != nullptr)
    {
        menu.addSeparator();
        menu.addItem (kLearnSwitchItem, "MAP THIS SWITCH...");
        menu.addItem (kRemoveItem, "REMOVE PEDAL");
    }

    // Open over the slot that was clicked, not at the board's corner: the menu belongs to that position.
    juce::Component* anchor = &board;

    if (index >= 0 && index < (int) slotViews.size())
        if (auto* view = slotViews[(size_t) index].get())
            anchor = view;

    // SafePointer: the menu can outlive this component (a quit with it open).
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                        [safe = juce::Component::SafePointer<BoardContent> (this), index, typeIds] (int result)
                        {
                            if (safe != nullptr)
                                safe->applySlotMenu (index, result, typeIds);
                        });
}

void BoardContent::applySlotMenu (int index, int result, const std::vector<juce::String>& typeIds)
{
    if (result == 0)
        return;

    if (result == kLearnSwitchItem)
    {
        if (auto* slot = model.at (index))
            beginLearn (slot->onParamIndex());

        return;
    }

    if (result == kRemoveItem)
    {
        removeSlotEffect (index);
    }
    else if (result >= 1 && result <= (int) typeIds.size())
    {
        const auto typeId = typeIds[(size_t) result - 1];

        if (model.at (index) != nullptr)
        {
            forgetPedalParams (index);
            model.replace (index, typeId);      // same slot, new pedal: the old values go with the old pedal
        }
        else
        {
            // A "+" past the last pedal fills the next position, not the one clicked (the model has no gaps
            // to skip); select where it actually went rather than the still-empty slot that was clicked.
            const int target = juce::jmin (index, model.size());

            if (model.insert (target, typeId))
            {
                syncSlots();
                selectSlot (target);
                return;
            }
        }
    }

    syncSlots();

    selectSlot (juce::jlimit (0, juce::jmax (0, model.size() - 1), board.selectedSlot()));
}

void BoardContent::toggleSlot (int index)
{
    if (auto* slot = model.at (index))
        if (slot->onParamIndex() != ParameterRegistry::kInvalid)
            registry.toggle (slot->onParamIndex());
}

void BoardContent::updateStatus()
{
    // Card 5.25: the tempo is device-wide, not a board's. The first pass adopts the saved value; after that any
    // move of the header's tempo is written back, so a relaunch opens at the tempo you left it at. A plugin has no
    // device-wide setting to keep: its tempo comes from the host or the saved state (cards 8.5, 8.6).
    if (host.tempoEditable())
    {
        if (const int tempoIndex = registry.find ("board/tempo"); tempoIndex != ParameterRegistry::kInvalid)
        {
            const float current = registry.get (tempoIndex);

            if (lastSavedTempo < 0.0f)
            {
                registry.set (tempoIndex, pb::settings::readTempo());
                lastSavedTempo = registry.get (tempoIndex);
            }
            else if (std::abs (current - lastSavedTempo) > 0.01f)
            {
                lastSavedTempo = current;
                pb::settings::writeTempo (current);
            }
        }
    }

    if (transientStatus.isNotEmpty() && juce::Time::currentTimeMillis() < transientUntil)
        board.setStatusText (transientStatus);
    else
        board.setStatusText (statusLine());

    // Card 5.24: each slot's own share of the block, for the thin bar above its footswitch.
    for (size_t i = 0; i < slotViews.size(); ++i)
        if (slotViews[i] != nullptr)
            slotViews[i]->setLoad (pb::load::slotLoad (model.at ((int) i)));

    updatePresetLabel();
}

// A short confirmation in the status line, since a saved or loaded board is otherwise silent.
void BoardContent::showTransient (const juce::String& text)
{
    transientStatus = text;
    transientUntil = juce::Time::currentTimeMillis() + 4000;
    updateStatus();
}

// Card 5.29: a board from scratch. An empty BoardData through the same apply() a load uses, so slots, macros
// and mappings clear together instead of three separate resets drifting apart. The tempo is untouched: it is
// the device's, not the board's (card 5.25).
void BoardContent::beginNewBoard()
{
    if (model.size() == 0)
    {
        startNewBoard();
        return;
    }

    ask ("New board",
         boardUnsaved ? "Start an empty board? This one has unsaved changes."
                      : "Start an empty board? The current one is already in its file.",
         "New board",
         [this] { startNewBoard(); });
}

void BoardContent::startNewBoard()
{
    const auto applied = pb::apply (pb::BoardData(), model, macros, learn, registry);

    currentPreset = juce::File();
    boardUnsaved = false;
    lastSeenCounter = registry.changeCounter();     // a fresh board is not an edit to write straight back out

    syncSlots();

    if (model.size() > 0)
        selectSlot (0);
    else
        editor.setSlot (nullptr);                   // no pedal to show, so the editor goes back to its panel

    saveWorkingBoard();                             // a relaunch opens the empty board, not the old one
    updateStatus();

    showTransient ("NEW BOARD");

    for (auto& warning : applied.warnings)
        juce::Logger::writeToLog ("pump BOARD: new board: " + warning);
}

void BoardContent::showPresetMenu()
{
    const auto files = presetManager.list();

    juce::PopupMenu menu;

    for (int i = 0; i < files.size(); ++i)
    {
        juce::PopupMenu item;
        item.addItem (kLoadBase + i, "LOAD");
        item.addItem (kRenameBase + i, "RENAME...");
        item.addItem (kDeleteBase + i, "DELETE...");
        menu.addSubMenu (presetManager.nameOf (files[i]), item);
    }

    if (files.isEmpty())
        menu.addItem (-1, "NO SAVED BOARDS", false, false);

    menu.addSeparator();
    menu.addItem (kSaveAsItem, "SAVE THIS BOARD AS...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton),
                        [safe = juce::Component::SafePointer<BoardContent> (this), files] (int result)
                        {
                            if (safe != nullptr)
                                safe->applyPresetMenu (result, files);
                        });
}

void BoardContent::applyPresetMenu (int result, const juce::Array<juce::File>& files)
{
    if (result == 0)
        return;

    if (result == kSaveAsItem)
    {
        promptForPresetName();
        return;
    }

    // Card 5.26: each board is a submenu, so a click can mean load, rename or delete.
    const bool isLoad = result >= kLoadBase && result < kLoadBase + 1000;
    const bool isRename = result >= kRenameBase && result < kRenameBase + 1000;
    const bool isDelete = result >= kDeleteBase && result < kDeleteBase + 1000;

    if (! isLoad && ! isRename && ! isDelete)
        return;

    const int index = result % 1000;

    if (index < 0 || index >= files.size())
        return;

    const auto file = files[index];

    if (isLoad)
        loadPresetFile (file);
    else if (isRename)
        promptRenamePreset (file);
    else
        promptDeletePreset (file);
}

void BoardContent::promptRenamePreset (const juce::File& file)
{
    const auto oldName = presetManager.nameOf (file);
    auto* window = new juce::AlertWindow ("Rename board", "New name for " + oldName + ":",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (&lookAndFeel);      // card 8.5: dialogs do not inherit the subtree's skin
    window->addTextEditor ("name", oldName);
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<BoardContent> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe, window, file] (int answer)
        {
            window->setLookAndFeel (nullptr);    // before JUCE deletes it

            if (answer != 1 || safe == nullptr)
                return;

            const auto typed = window->getTextEditorContents ("name").trim();

            if (typed.isEmpty() || typed == safe->presetManager.nameOf (file))
                return;

            if (! safe->presetManager.rename (file, typed))
            {
                safe->showTransient ("COULD NOT RENAME");
                return;
            }

            // The board on screen keeps pointing at the file it came from under its new name.
            if (safe->currentPreset == file)
                safe->currentPreset = safe->presetManager.fileFor (typed);

            safe->updatePresetLabel();
            safe->showTransient ("RENAMED   " + typed.toUpperCase());
        }), true);
}

void BoardContent::promptDeletePreset (const juce::File& file)
{
    const auto name = presetManager.nameOf (file);

    ask ("Delete board",
         "Delete \"" + name + "\"? The file goes to the Bin, so it can be recovered.",
         "Delete",
         [this, file, name]
         {
             if (! presetManager.remove (file))
             {
                 showTransient ("COULD NOT DELETE");
                 return;
             }

             // A deleted board cannot stay "the one SAVE overwrites".
             if (currentPreset == file)
                 currentPreset = juce::File();

             updatePresetLabel();
             showTransient ("DELETED   " + name.toUpperCase());
         });
}

// The one place a preset reaches the board, so the header keys, the menu, MIDI and the keys behave alike.
void BoardContent::loadPresetFile (const juce::File& file)
{
    BoardData data;
    LoadResult result;

    if (! presetManager.read (file, data, result))
    {
        showTransient ("COULD NOT LOAD THAT BOARD");
        juce::Logger::writeToLog ("pump BOARD: could not load " + file.getFullPathName() + ": " + result.error);

        juce::StringArray problem;
        problem.add (result.error.isNotEmpty() ? result.error : juce::String ("the file could not be read"));
        showWarnings (problem, "COULD NOT LOAD " + presetManager.nameOf (file).toUpperCase());
        return;
    }

    // The swap happens between the bypass fade, so loading while playing does not click.
    beginSwap ([this, file, data, result]
    {
        const auto applied = pb::apply (data, model, macros, learn, registry);
        const auto name = presetManager.nameOf (file);

        currentPreset = file;
        syncSlots();
        selectSlot (0);
        boardUnsaved = false;
        lastSeenCounter = registry.changeCounter();   // loading is not an edit to write straight back out
        saveWorkingBoard();                          // so a relaunch opens this board, not the old one
        updateStatus();

        showTransient ("LOADED   " + name.toUpperCase());
        juce::Logger::writeToLog ("pump BOARD: loaded preset " + name);

        juce::StringArray warnings (applied.warnings);
        warnings.addArray (result.warnings);
        showWarnings (warnings, "LOADED " + name.toUpperCase() + " WITH PROBLEMS");
    });
}

// < and > step through the saved boards, wrapping at either end.
void BoardContent::stepPreset (int direction)
{
    const auto files = presetManager.list();

    if (files.isEmpty())
    {
        showTransient ("NO SAVED BOARDS");
        return;
    }

    int current = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i] == currentPreset)
            current = i;

    const int next = current < 0 ? (direction > 0 ? 0 : files.size() - 1)
                                 : (current + direction + files.size()) % files.size();

    loadPresetFile (files[next]);
}

// SAVE overwrites the board you are on; with nothing named yet it asks for a name instead.
void BoardContent::quickSave()
{
    if (currentPreset.getFullPathName().isEmpty())
    {
        promptForPresetName();
        return;
    }

    if (savePresetTo (currentPreset))
        showTransient ("SAVED   " + presetManager.nameOf (currentPreset).toUpperCase());
    else
        showTransient ("COULD NOT SAVE");
}

void BoardContent::toggleBoardBypass()
{
    if (boardBypassIndex == ParameterRegistry::kInvalid)
        return;

    registry.toggle (boardBypassIndex);
    updateBypassLight();
}

void BoardContent::updateBypassLight()
{
    board.setBypassActive (boardBypassIndex != ParameterRegistry::kInvalid
                           && registry.get (boardBypassIndex) >= 0.5f);
}

void BoardContent::handleHeaderKey (int key)
{
    switch (key)
    {
        case BoardPrototype::kPrevPreset: stepPreset (-1); break;
        case BoardPrototype::kNextPreset: stepPreset (1);  break;
        case BoardPrototype::kSave:       quickSave();     break;
        case BoardPrototype::kBypass:     toggleBoardBypass(); break;
        default: break;
    }
}

// Card 3.2: a mapped key asked for a preset. Same preset manager the header keys use.
void BoardContent::handleControlEvent (const ControlEvent& event)
{
    switch (event.kind)
    {
        case ControlEvent::Kind::PresetNext:    stepPreset (1);               break;
        case ControlEvent::Kind::PresetPrev:    stepPreset (-1);              break;
        case ControlEvent::Kind::PresetSelect:  selectPreset (event.value);   break;
        case ControlEvent::Kind::LearnCaptured: break;      // learn is the message thread's own business
        default: break;
    }
}

// A program number (or a key) picks a saved board by position: 0 is the first in the list.
void BoardContent::selectPreset (int number)
{
    const auto files = presetManager.list();

    if (! juce::isPositiveAndBelow (number, files.size()))
    {
        showTransient ("NO BOARD " + juce::String (number + 1));
        return;
    }

    loadPresetFile (files[number]);
}

// Card 3.3: arm learn for a control and say so where the user is looking.
void BoardContent::beginLearn (int paramIndex)
{
    if (paramIndex == ParameterRegistry::kInvalid)
        return;

    learn.beginLearn (paramIndex);
    editor.setLearnTarget (paramIndex);
    showTransient ("LEARN   " + learn.describeTarget (paramIndex) + "   PLAY A SWITCH OR PRESS A KEY");
}

// The source was captured; ask what it should do, with the useful answer already ticked.
void BoardContent::askForAction()
{
    const int param = learn.pendingParamIndex();

    if (param == ParameterRegistry::kInvalid)
    {
        learn.discardPending();
        return;
    }

    const auto preferred = learn.defaultActionFor (param);

    juce::PopupMenu menu;
    menu.addSectionHeader (learn.describeSource (learn.pendingSource()));
    menu.addItem (kMapToggle, "Toggle", true, preferred == ActionType::Toggle);
    menu.addItem (kMapMomentary, "Momentary", true, preferred == ActionType::Momentary);
    menu.addItem (kMapThreshold, "Threshold (CC 64)", true, preferred == ActionType::CCThreshold);
    menu.addItem (kMapValue, "Value", true, preferred == ActionType::SetValue);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editor),
                        [this, safe = juce::Component::SafePointer<BoardContent> (this), param] (int result)
    {
        if (safe == nullptr)
            return;                       // the window went while the menu was open

        switch (result)
        {
            case kMapToggle:    learn.commitPending (ActionType::Toggle);      break;
            case kMapMomentary: learn.commitPending (ActionType::Momentary);   break;
            case kMapThreshold: learn.commitPending (ActionType::CCThreshold); break;
            case kMapValue:     learn.commitPending (ActionType::SetValue);    break;
            default:            learn.discardPending(); return;
        }

        editor.setLearnTarget (ParameterRegistry::kInvalid);
        showTransient ("MAPPED   " + learn.describeTarget (param));
    });
}

// A param's name as the musician knows it: the pedal it belongs to, then the control.
juce::String BoardContent::labelForParam (int paramIndex) const
{
    for (int i = 0; i < model.size(); ++i)
    {
        auto* slot = model.at (i);

        if (slot == nullptr)
            continue;

        auto* effect = slot->getEffect();
        const auto pedal = effect != nullptr ? effect->displayName().toUpperCase() : juce::String ("SLOT");

        if (slot->onParamIndex() == paramIndex)
            return pedal + " SWITCH";

        const auto& indices = slot->paramIndices();

        if (effect != nullptr)
        {
            const auto params = effect->params();

            for (size_t p = 0; p < indices.size() && p < params.size(); ++p)
                if (indices[p] == paramIndex)
                    return pedal + " " + params[p].name.toUpperCase();
        }
    }

    return registry.info (paramIndex).displayName.toUpperCase();
}

void BoardContent::toggleMappings()
{
    if (mappingsView == nullptr)
    {
        mappingsView = std::make_unique<MappingsView> (learn);
        mappingsView->onClose = [this]
        {
            if (mappingsView != nullptr)
                mappingsView->setVisible (false);
        };

        addAndMakeVisible (*mappingsView);
        mappingsView->setBounds (getLocalBounds());
    }
    else
    {
        mappingsView->setVisible (! mappingsView->isVisible());
    }

    if (mappingsView->isVisible())
    {
        mappingsView->refresh();
        mappingsView->toFront (true);
    }
}

void BoardContent::promptForPresetName()
{
    auto* window = new juce::AlertWindow ("Save board", "Name this board:", juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (&lookAndFeel);      // card 8.5: dialogs do not inherit the subtree's skin
    window->addTextEditor ("name", "Board " + juce::String (presetManager.list().size() + 1));
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, safe = juce::Component::SafePointer<BoardContent> (this), window] (int result)
    {
        window->setLookAndFeel (nullptr);        // before JUCE deletes it

        if (safe == nullptr)
            return;

        if (result != 1)
            return;

        const auto name = window->getTextEditorContents ("name").trim();

        if (name.isEmpty())
            return;

        const auto file = presetManager.fileFor (name);

        if (savePresetTo (file))
        {
            const auto saved = presetManager.nameOf (file);
            showTransient ("SAVED   " + saved.toUpperCase());
            juce::Logger::writeToLog ("pump BOARD: saved preset " + saved);
        }
        else
        {
            showTransient ("COULD NOT SAVE");
        }
    }), true);
}

// ---- Card 4.3: presets ----

// The working copy is the live state: it is what a relaunch opens, so an edit made on top of a preset is
// not lost by the app closing. The named file is the snapshot SAVE writes.
bool BoardContent::loadWorkingBoard()
{
    if (! hasWorkingCopy())
        return false;

    BoardData data;
    LoadResult result;

    if (! presetManager.read (host.workingBoardFile(), data, result))
        return false;

    const auto applied = pb::apply (data, model, macros, learn, registry);

    if (! applied.ok)
    {
        juce::Logger::writeToLog ("pump BOARD: could not restore the last board: " + applied.error);
        return false;
    }

    // The file remembers which preset it came from, so SAVE overwrites that one rather than asking again.
    if (data.name.isNotEmpty())
    {
        const auto candidate = presetManager.fileFor (data.name);

        if (candidate.existsAsFile())
            currentPreset = candidate;
    }

    juce::Logger::writeToLog ("pump BOARD: restored " + juce::String (model.size()) + " pedal(s), "
                              + juce::String ((int) learn.mappings().size()) + " mapping(s)");

    juce::StringArray warnings (applied.warnings);
    warnings.addArray (result.warnings);
    showWarnings (warnings, "THE LAST BOARD WAS NOT QUITE AS SAVED");

    return true;
}

bool BoardContent::saveWorkingBoard()
{
    if (! hasWorkingCopy())
        return false;

    auto data = pb::capture (model, macros, learn, registry);
    data.name = currentPreset.getFullPathName().isEmpty() ? juce::String()
                                                        : presetManager.nameOf (currentPreset);

    return presetManager.write (host.workingBoardFile(), data);
}

bool BoardContent::savePresetTo (const juce::File& file)
{
    auto data = pb::capture (model, macros, learn, registry);
    data.name = presetManager.nameOf (file);

    if (! presetManager.write (file, data))
        return false;

    currentPreset = file;
    boardUnsaved = false;
    saveWorkingBoard();                 // the working copy follows, so a relaunch opens what was saved
    updateStatus();
    return true;
}

// The fade is the board's own bypass smoother: bypass, swap, unbypass. A board the user left bypassed stays
// bypassed, and the swap itself never lands on a live chain.
void BoardContent::beginSwap (std::function<void()> swap)
{
    // A load already waiting on the fade: the newest one replaces it (two quick program changes, or > > on the
    // header). Without this the second ran at once — the board read as bypassed, by us — and the first then
    // landed on top of it 25 ms later.
    if (pendingSwap != nullptr)
    {
        pendingSwap = std::move (swap);
        return;
    }

    if (boardBypassIndex == ParameterRegistry::kInvalid || registry.get (boardBypassIndex) >= 0.5f)
    {
        swap();
        return;
    }

    registry.set (boardBypassIndex, 1.0f);
    pendingSwap = std::move (swap);
    pendingSwapDue = juce::Time::currentTimeMillis() + 25;      // longer than the 10 ms ramp
}

void BoardContent::updatePresetLabel()
{
    const bool untitled = currentPreset.getFullPathName().isEmpty();

    board.setPresetName (untitled ? juce::String ("UNTITLED")
                                  : presetManager.nameOf (currentPreset).toUpperCase(),
                         boardUnsaved);
    board.setStatusRight (untitled ? "NOT SAVED YET" : juce::String ("~/MUSIC/PUMP BOARD/BOARDS"));
}

void BoardContent::showWarnings (const juce::StringArray& messages, const juce::String& title)
{
    if (messages.isEmpty())
        return;

    if (warningBar == nullptr)
    {
        warningBar = std::make_unique<WarningBar>();
        warningBar->onClose = [this] { if (warningBar != nullptr) warningBar->setVisible (false); };
        addAndMakeVisible (*warningBar);
    }

    warningBar->show (messages, title);
    warningBar->setBounds (getLocalBounds().removeFromBottom (96).reduced (12, 6));
    warningBar->toFront (true);
}

juce::String BoardContent::statusLine() const
{
    const auto summary = host.getSummary();

    if (! summary.hasAudio)
    {
        auto text = juce::String ("NO AUDIO DEVICE");

        if (summary.error.isNotEmpty())
            text << "    " << summary.error;

        return text;
    }

    auto text = juce::String (summary.sampleRateHz / 1000.0, 1) + " kHz    "
              + juce::String (summary.blockSize) + " smp";

    if (summary.roundTripKnown)
        text << "    ROUND TRIP " << juce::String (summary.roundTripMs, 1) << " ms";

    // Card 5.6: what the board itself adds. The device's round trip cannot know about a pedal's lookahead,
    // oversampler or FFT, so a board with COMP or CRUSH on it is later than the number beside it suggests.
    int chainLatency = 0;
    const int slotCount = (int) model.size();

    for (int i = 0; i < slotCount; ++i)
        if (auto* slot = model.at (i))
            if (auto* fx = slot->getEffect())
                chainLatency += fx->latencySamples();

    if (chainLatency > 0)
        text << "    BOARD " << juce::String ((double) chainLatency * 1000.0
                                             / juce::jmax (1.0, processor.getSampleRateHz()), 1) << " ms";

    // Card 5.24: the chain's total effect cost, the sum of what each slot spends. A guide, not a promise:
    // it is measured on this machine at this buffer size, and the device adds its own work on top.
    const float boardLoad = pb::load::boardLoad();

    if (boardLoad > 0.005f)
        text << "    LOAD " << juce::String (juce::jmin (100.0f, boardLoad * 100.0f), 0) << "%";

    if (summary.fallback)
        text << "    FALLBACK";

#ifdef PB_ALLOCATION_GUARD
    // Card 2.4: Debug-only badge. Silence here is the proof that the audio thread is clean.
    if (const int allocations = pb::alloc::violations(); allocations > 0)
        text << "    ALLOC " << allocations;
#endif

    return text;
}

void BoardContent::toggleSettings()
{
    // A plugin's host owns the device. There is nothing here to pick, and the button is hidden anyway.
    if (! host.hasSettingsView())
        return;

    if (settingsView == nullptr)
    {
        settingsView = host.createSettingsView ([this] { hideSettings(); });

        if (settingsView == nullptr)
            return;

        addAndMakeVisible (*settingsView);
        settingsView->setBounds (getLocalBounds());
    }
    else
    {
        settingsView->setVisible (! settingsView->isVisible());
    }

    if (settingsView->isVisible())
        settingsView->toFront (true);
}

void BoardContent::hideSettings()
{
    if (settingsView != nullptr)
        settingsView->setVisible (false);
}

// A question with two answers, skinned and parented to this content. The dialogs are the one place the app's
// process-wide look-and-feel used to do the work for us; the plugin must not have one, so it is done by hand.
void BoardContent::ask (const juce::String& title, const juce::String& message, const juce::String& confirm,
                        std::function<void()> onConfirm)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, this);
    window->setLookAndFeel (&lookAndFeel);
    window->addButton (confirm, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<BoardContent> (this), window, onConfirm] (int result)
        {
            window->setLookAndFeel (nullptr);   // before JUCE deletes it, so ours is never held by a dead window

            if (result == 1 && safe != nullptr && onConfirm)
                onConfirm();
        }), true);
}
} // namespace pb
