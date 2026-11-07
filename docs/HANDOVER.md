# pump BOARD — handover

A Mac-only JUCE standalone guitar pedalboard: sixteen slots in series, eight macro knobs, MIDI and key
mapping, presets. pump's DSP is compiled in as source; nothing in pump itself is edited.

This file is the state of play and what to do next. `docs/NOTES.md` holds the reasoning behind every
decision and deviation — read it before changing anything, it is where the traps are written down.

- **Where we are:** phases 0–4 are complete (card 0.4's probe closed the last gap) and phase 5 has begun with
  CRUSH, SWIRL and COMP. Boards are `.board` JSON files in `~/Music/pump BOARD/Boards`, a working copy reopens on launch with slots, macros and
  mappings, and the app can be played: sixteen slots, eight macros, MIDI and key mappings, presets with
  program changes, and a measured round trip in Settings. What is left is hand-testing
  (`REVIEW_TEMPLATE.md`) and phases 5–7 (more pedals, custom pedals, the full skin).
- **Tests:** 3212 expectations in Debug, 0 failing. Last run: 6 Nov 2026 on **Linux** in a cloud
  session (see "Building on Linux" below) — not yet re-run on the Neo.
- **Latest pass (6 Nov 2026):** a senior-style review fixed a use-after-free on pedal swap/remove/device change,
  stale mappings after a pedal goes, empty slots vanishing on load, a click when a latency pedal is switched off,
  and a dozen smaller bugs; then the whole app was reskinned to Thunder Plus's **IDM PINK & GREEN**. The full list
  is the last entry in `NOTES.md`; what the review found but did not fix is under "Open from the review" below.
- **Plan:** `docs/tasks/phase-0.md` … `phase-5-6-7.md`, one card at a time.

---

## Build, test, run

JUCE and pump are **submodules**: clone with `git clone --recurse-submodules` (or run
`git submodule update --init --recursive` in an existing checkout). They are pinned to JUCE 9.0.5
(`d60b7a7`) and pump `7d981aa` — the exact commits this app is built and tested against. Configure with
`-DCMAKE_BUILD_TYPE=Debug`: that enables `PB_ALLOCATION_GUARD`, which is where the 20 expectations in
`AllocationGuardTests` come from, so a plain configure runs fewer and is otherwise clean.

```bash
cd pump-board
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk   # required on this machine
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_SYSROOT="$SDKROOT"   # first time only
cmake --build build
```

```bash
./build/PumpBoardTests_artefacts/Debug/PumpBoardTests          # 3212 expectations
open "build/PumpBoard_artefacts/Debug/pump BOARD.app"          # or double-click it in Finder
```

Debug matters: the audio-thread allocation guard (`PB_ALLOCATION_GUARD`) is a Debug-only feature, and it is
the proof that section 3 of the house rules holds.

Offscreen renders, for looking at the UI without an audio device:

```bash
BIN="build/PumpBoard_artefacts/Debug/pump BOARD.app/Contents/MacOS/pump BOARD"
"$BIN" --render board.png 2.0                               # the painted design mockup
"$BIN" --render-slots slots.png 2.0                         # the real sixteen slots
"$BIN" --render-editor editor.png 2.0 pump.tube             # one real pedal + its knob panel
```

Read the PNGs; delete them afterwards. They are the only way to see the UI from a terminal — the test
target does not link the UI sources.

---

## What works today

- **Audio:** device open with fallback, saved setup, mono input folded up, input trim → 16 slots in series →
  output trim → board bypass (10 ms equal-power) → safety limiter (−0.3 dBFS, stereo-linked, instant attack).
- **Pedals:** twenty-four — `pump.tune`, `pump.vox`, `pump.grumble` (a "voice" category, 6 Nov 2026; null-tested
  like the rest), plus `pump.spring`, `pump.echo`, `pump.tube`, `pump.crush`, `pump.swirl`, `pump.comp`,
  `pump.heat`, `pump.tone`, `pump.verb`, `pump.limit`, `pump.snap`, `pump.strip`, `pump.calm`, `pump.gate`,
  `pump.grain`, `pump.stutter`, `pump.chop`, `pump.wide`, `pump.band`, `pump.eq`, `pump.twang` as `IEffect` adapters;
  METER and MASTER are deliberately absent, see NOTES' phase-5 closing entry. The five tempo-locked ones
  (SWIRL, GATE, GRAIN, STUTTER, CHOP)
  free-run at their adapter's own 120 BPM: the board's tempo does not reach effects yet — see NOTES, last entry. Bit-identical to pump's own engines (null tests: difference exactly 0.0 at 64/128/511 samples,
  defaults and edited, with one exception: WIDE's balanced two-band setting measured 2.5e-9, about −172 dBFS — see NOTES). TONE, CALM, HEAT, STRIP, LIMIT, TUBE and CRUSH report latency by default — TONE and CALM
  the most at their default FFT — and SNAP when its lookahead is on; SPRING, ECHO and VERB never do. The dry paths
  compensate and the status line shows the total — see the landmine.
- **Board:** insert / replace / remove / move, snapshot publishing with retire-at-N+2 garbage collection,
  sixteen real `SlotView`s, drag to reorder, category-grouped pedal picker, selection, per-pedal on/off, and a
  thin CPU bar per slot plus a `LOAD` figure in the status line (card 5.24).
- **Skin:** one skin, Thunder Plus's **IDM PINK & GREEN** (6 Nov 2026; the old dark/light pump palettes and the
  Settings SKIN switch are gone). Paper `#E9E8E3`, ink `#141414`, pink `#FF2D8A`, green `#1F9E5A`, flat 1.5 px ink
  outlines, JetBrains Mono everywhere. The palette lives in `style::idm` / `style::Theme` (`PumpStyle.h`); every
  stock JUCE widget is dressed by `IdmLookAndFeel.h`, set as the default in `Main.cpp`. Pedal category colour:
  pink for DRIVE/DYNAMICS/FILTER/UTIL, green for TIME/SPACE/MOD.
- **Editing:** `GenericEffectEditor` builds a panel from any effect's `params()`; values only ever move
  through `ParameterRegistry`; double-click resets to default; skewed params honoured.
- **Presets:** boards are the JSON `.board` files in `~/Music/pump BOARD/Boards`, listed sorted by name —
  that order is the program-change number. A working copy (`~/Music/pump BOARD/current.board`) is written a
  moment after the board stops changing and on quit, so a relaunch reopens exactly what you left, macros and
  mappings included. `SAVE` overwrites the board you are on, `<` `>` step the list, the PRESETS button lists
  them and offers "SAVE THIS BOARD AS...", and the header shows the board's name with a star while it has
  unsaved changes. A swap fades through the board bypass so a load while playing does not click.
- **Taking a pedal off:** a small × in the pedal's own top-right corner, or REMOVE PEDAL in its right-click menu.
  It empties the *position* (card 5.28) rather than shifting the board down, so the empty "+" takes the next pedal
  and macros/mappings keep pointing where they did.
- **A new board:** the NEW BOARD button in the side column clears slots, macros and mappings through the same
  `apply()` a preset load uses, asks first when a board is loaded, and leaves the tempo alone (card 5.29).
- **Tempo:** `board/tempo` (20–300 BPM) in the header, drag or double-click to type; reaches every pedal that
  implements `ITempoAware` — seven of them (SWIRL, ECHO, VERB, GATE, GRAIN, STUTTER, CHOP — card 5.22). It is
  device-wide (card 5.25): not in a board, and remembered across launches in `~/Music/pump BOARD/settings`.
- **Learn and mappings:** right-click a knob or switch → Reset / Type a value / Learn; Learn arms both
  sources, takes the next MIDI message or key, and asks Toggle / Momentary / Threshold / Value with the useful
  one ticked. The MAPPINGS button lists every mapping (source, action, target, range) and clicking a row
  changes its action, edits its range or deletes it. Esc cancels. The slot footswitch is mappable from its own
  menu. All of it runs on one mapping table the router swaps and retires like the chain.
- **Macros:** eight knobs in the strip along the bottom, each a registry param (`macro/0..7`) so it is a
  control like any other. Right-click: Add target (then click a knob and give it low, high and a shape),
  Rename, Clear targets. A target follows its knob through linear, exponential, square-root or inverted
  mapping, and only when the knob actually moved.
- **Limiter and allocation proof:** sixteen +12 dB pedals from 0.25 peak land at exactly −0.30 dBFS; a
  sixteen-slot board allocates **0** times per block, warm-up included.

## What is implemented but never exercised by a human

Nothing automated can press a Mac button here, so these are all "wired, not proven":

- Every click in the UI: footswitch, select, right-click menu, drag and drop, the pedal picker, the preset
  menu and name dialog, the four header keys, and the tempo field's drag and typed entry.
- That a knob turned in the running app reaches `board.xml` (the value-level round trip is tested; the GUI
  path is not).
- How any of it sounds. The null tests prove the adapters are faithful; nobody has judged the board.
- Any real MIDI or key press. Learning is proven at the controller level, but no footswitch has been plugged
  into this app and no key has been pressed in its window; the focus policy, the text-field guard and the
  keyboard poll are argued from the code.
- Every click of the new UI: the MAPPINGS overlay, the row menu, the range and type dialogs, the LEARN
  outline, the slot's "MAP THIS SWITCH..." item. The app was launched, not driven.
- Any macro interaction: its menus, the add-target pick, the rename dialog, or a knob actually moving a
  pedal's control. The strip was rendered and looked at, not turned.
- The preset path by hand: the PRESETS menu, Save As, SAVE, `<`/`>`, the warning bar's DISMISS, and whether
  the bypass fade is actually inaudible. The app launched twice on its own and drove itself.
- A MIDI clock. Card 5.32 proves the route end to end through `BoardProcessor` with synthetic clock bytes, but
  no hardware clock has been plugged into this app, and no real footswitch either.

If you find something in this list misbehaving, say what happened and it gets fixed — that is cheaper than
guessing at interaction quality from code.

---

## Decisions still open for the owner

1. **JUCE version.** `external/JUCE` is a shallow clone of master = **9.0.5**, not the 8.x the plan was
   written for. Pin to 8.x (recommended, the API and licence terms match the plan) or adopt 9 deliberately.
   Also: make it a submodule before this is shared; it is gitignored today.
2. **Choice labels.** `ParamInfo` has no label list, so pump's named modes show as numbers: spring `model`
   0–5, echo `mode`/`stereo`/`note_l`/`note_r`/`pitch_mode`, tube `mode`/`focus`/`oversampling`. Either add
   an optional choices list to the scaffold struct (needs review) or let adapters supply their own editors.
   This is the largest remaining gap between the app and pump's own plugins.
3. **Latency compensation — the mid-play half is settled.** The owner asked for no oversampling change mid play,
   and card 5.33 now holds the change back until the input has been silent, so a pedal's latency can no longer
   move under a held note. What is still open is compensating the chain: a TUBE, CRUSH or COMP slot still
   delays everything after it. Decide before building anything that aligns phases.
4. **Round-trip measurement.** Card 0.4's click probe needs a loopback cable. The status line shows the
   device's *reported* latency (46.8 ms on the built-in speakers at 64 samples) — not a measurement.
5. **Preset format — settled, one loose end.** Phase 4 moved boards to the JSON `.board` format, so the
   `<PUMPBOARD>` XML path is now only a one-time import of `board.xml`. Decide whether to delete
   `BoardPreset`/`PresetLibrary` and their tests, or keep them as the migration path for the next release.
6. **Fonts and distribution.** Settled (card 5.27; since 6 Nov only JetBrains Mono Regular and Bold): the TTFs are compiled into the app with
   `juce_add_binary_data`, so it no longer reads `external/pump` at runtime and looks right on a Mac that has
   neither face installed; the `OFL-*.txt` licences are in the bundle's `Resources`.

---

## Next steps, in order

### 1. Card 3.1 — MidiRouter core (done 2026-11-07)

`handle` builds a `MappingSource` per note / CC / program change and applies every matching row; `publish` /
`collectGarbage` retire at N+2; learn captures one message and routes nothing while waiting. Thirteen tests in
`Tests/MidiRouterTests.cpp` cover each action, the channel filter, learn, the FIFO dropping when full, and a
swapped table being freed. The fixed header has no block counter or retire list, so both live in a
per-instance table inside the `.cpp`; details and the CC edge-case limits are in `NOTES.md`.

### 2. Card 3.2 — Keys (done 2026-11-07)

`KeyRouter` tracks what is held (so Momentary holds a switch only while its key is down), swallows auto-repeat,
leaves keys alone while a text field has focus, and hands preset actions straight to the preset manager instead
of the audio FIFO. It is GUI-free and driven through `MidiRouter::handleKey`, so the suite covers it; the
window supplies the key codes and the "still down" poll. Eight tests in `Tests/KeyRouterTests.cpp`.

### 3. Card 3.3 — Learn UI (done 2026-11-07)

Right-click a knob or switch → Reset / Type a value / Learn; Learn takes the next note, CC, program change or
key and asks which action it should perform, defaulting to Toggle for a switch and Value for a knob. The
MAPPINGS button shows every mapping and lets a row's action, range or existence be changed. Esc cancels. The
controller lives in `Source/Control` (not `Source/UI` as the card says) so the suite can prove the handshake;
the reasoning is in `NOTES.md`, along with the two limits: mappings are not saved yet, and the same source onto
the same target replaces rather than duplicates.

### 4. Card 3.4 — MacroBank and the macro strip (done 2026-11-07)

`MacroBank` registers `macro/0..7`, keeps a name and a target list per macro, maps through linear /
exponential / square-root / inverted curves, and writes a macro's targets when its knob moves. The painted
strip is gone: eight real knobs with names, target counts, a right-click menu and a target popup. Five tests
cover the curves, two targets, update-on-change and the names. Two gaps are logged in `NOTES.md`: there is no
per-target list view (a range or curve cannot be changed after adding it), and a macro knob cannot be learned
onto MIDI or a key from the UI yet.

### Phase 4, cards 4.1 and 4.2 (done 2026-11-07)

`BoardSerializer.cpp` writes and reads the schema's JSON; `BoardState.cpp` bridges the live board (slots,
macros, mappings, trims) to that data. `docs/NOTES.md` has the decisions: six decimal places, slot references by
position rather than uid, dropped targets warned about rather than guessed at, and the tempo gap below. Both
suites are in the test target.

### 4.3 — Card 4.3, presets on disk (done 2026-11-07)

`PresetManager` owns `~/Music/pump BOARD/Boards/*.board` (sorted by name, which is the program-change
number) and `WarningBar` shows what a load had to drop. The header carries the board's name and a star while
it is unsaved, a working copy is written shortly after the board stops changing, and a swap goes through the
board bypass so it does not click. Five new tests take a live board with a macro and a mapping through the
file and back; the app was launched twice and the second launch reopened the board the first one wrote.

### 5. Review, then later phases

`docs/REVIEW_TEMPLATE.md` is written: every card of phases 0–4 with its status, its evidence and its gaps, then
the hand-test checklist in the order worth trying it. What remains is the hand-test pass itself — everything
is wired and much of it is only launched, never driven by hand (the list is above, under "What is implemented  but never exercised by a human"). After that, phase 5–7 continues the pedal list — `pump CRUSH` is the first
  of them, and `PUMP_DSP_MAP.md` says which are copy-paste adapters — then custom modular pedals, then the skin.

### 5. Small, worth doing soon

- There is no "revert to the board I had before loading". Renaming and deleting a board from the PRESETS menu
  arrived in card 5.26 (both go through the Bin).
- Nothing restores the BPM a board was *saved* at, on purpose: the tempo is the device's (card 5.25) and lives
  in `~/Music/pump BOARD/settings`, not in the board.
- `--render-slots` still draws the prototype's painted editor placeholder, which invents knob names. It is
  only a render, but it is a lie sitting in a tool.
- `BoardPrototype`'s own slot hit-testing, drag and drop is **unreachable** now that `SlotView`s cover the
  same rectangles. Delete it once the painted fallback is retired, so there are not two implementations.
- Tap tempo for `board/tempo`. MIDI clock arrived in card 5.32 (24 ppqn at the MIDI input, allocation-free);
  a tap button is the same arithmetic without the wire.
- A Release build with a name and an icon, so the app is a normal double-clickable Mac app instead of a white
  icon three folders deep in `build/`.

### 6. Later phases

Phase 5–7 (`docs/tasks/phase-5-6-7.md`): more pedals from pump, custom modular pedals (`Source/Modular/
PedalGraph.cpp` is a stub), and the full skin. `PUMP_DSP_MAP.md` maps all 25 pump effects and says which are
copy-paste adapters and which are not (TWANG has no engine of its own — it drives COIL's, treat it as its own
card).

---

## Still open (after the 6 Nov review and open-items pass)

Closed on 6 Nov: change-only `dirty` in every adapter (`AssignHelpers.h`), sample-rate-sized dry rings, UI repaint on
change, paged editor for dense pedals, `PresetManager` name/rename edges, `MidiRouter` router limit. Details in the
last `NOTES.md` entries. Left:

1. **A pedal switched back on replays its old tail.** A settled-off slot freezes its pedal's state rather than
   clearing it. Fix with a fresh successor (`makeSuccessor`) once a slot has been off for ~50 ms, never `reset()`
   on the audio thread.
2. **`MainContent` is a god object** (~1300 lines: layout, autosave, presets, swap state machine, learn flow, four
   menus, status text, tempo, overlays). Split out a `BoardSession` (autosave, presets, swap) and a `SlotMenus`
   helper before adding more to it. `BoardPrototype`'s painted slots/editor duplicate the real components.
3. **TO DO — add pump AMP (owner approved the schema change, 6 Nov 2026; paused before starting).** The most
   guitar-relevant pump effect. What it needs:
   - **Dependencies:** pump's `third_party/NeuralAmpModelerCore` (nested submodule, MIT) and, inside it,
     `Dependencies/eigen` and `Dependencies/nlohmann`. pump's AMP Makefile builds with `-DNAM_SAMPLE_FLOAT
     -I<NAM> -isystem <eigen> -I<NAM>/Dependencies/nlohmann` and compiles the NAM core as one unit through
     `plugins/amp/nam_unity.cpp`; mirror that in `CMakeLists.txt`. In the cloud session the NAM core cloned fine but
     Eigen (hosted on gitlab.com) did not come through the proxy — on the Mac,
     `git submodule update --init --recursive` inside `external/pump` should fetch it.
   - **Adapter:** `PumpAmpAdapter` around `pump::amp::Engine` (input, gate, normalize, mono/stereo, bass/mid/treble/
     presence, cab on, cab low/high cut, output). Models and IRs load **off the audio thread** through the engine's
     own `loadModel` / `loadIr` (it swaps them at a block boundary with a fade); never load from `process()`.
     Latency = the engine's resampler + 64-sample cabinet block, reported whether the cab is on or off.
   - **UI:** a LOAD MODEL and a LOAD CAB key in the AMP editor (a `FileChooser`, `.nam` and `.wav`), showing the
     loaded file names; empty means no model / no cab.
   - **Saving (approved):** board JSON `version: 2` with an optional per-slot `files` object, e.g.
     `"files": { "model": "/path/x.nam", "ir": "/path/cab.wav" }`. The reader must still accept version 1; update
     `schemas/board.schema.json`, `BoardSerializer`, `BoardState` (capture/apply) and their tests. A missing file
     on load is a warning in the warning bar, not a failure.
   - **Tests:** a null test against `pump::amp::Engine` with no model (and, if feasible, with
     `third_party/NeuralAmpModelerCore/example_models/lstm.nam`), a v1-and-v2 round trip, a missing-file warning.
4. **Pages in the editor are 20 controls max**, two rows of ~32 px dials on dense pages (VOX has 3 pages, GRUMBLE,
   ECHO and TUNE 2). Clicking the page keys has only been rendered, not clicked in a live window.
5. **A pedal that blows up internally stays silent until rebuilt.** Non-finite input can no longer reach a pedal
   (`Common/Sanitize.h`), and a NaN a pedal makes is stopped at its own output, but that pedal's state stays
   poisoned. A watchdog could notice repeated scrubs from one slot and rebuild its pedal from the message thread
   via `Slot::makeSuccessor`, carrying its param values over (and keeping its mappings valid).
6. **Not on the board, on purpose:** METER and MASTER (owner's call), ROOM (system correction, not a pedal), the
   synths (no MIDI into slots), MACHINE (GPL, house rule).
7. **JUCE licence.** JUCE is AGPLv3 or commercial. Fine for personal use; if pump BOARD is ever sold or shipped
   closed-source, a commercial JUCE licence is needed.

## Building on Linux (cloud agents)

The app is Mac-only, but the suite and the offscreen renders build on Linux for an agent without a Mac. JUCE's
missing system headers are switched off at configure time:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-DJUCE_ALSA=0 -DJUCE_JACK=0 \
  -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_XINPUT=0"
cmake --build build --target PumpBoardTests && ./build/PumpBoardTests_artefacts/Debug/PumpBoardTests
cmake --build build --target PumpBoard
xvfb-run -a "build/PumpBoard_artefacts/Debug/pump BOARD" --render-slots slots.png 1.5
```

Run one suite with `PumpBoardTests WideNull` (any part of the suite name). Always check a **Release** build too
(`cmake -B build-rel -DCMAKE_BUILD_TYPE=Release ...`): CI builds Release, and timing-dependent tests (the linear-phase
splitters' worker threads) have passed in Debug and failed in Release before.

GCC warns about a few things Apple clang does not (`-Wshadow` in tests, `-Waddress`); the zero-warning rule is
judged on the Mac build.

## Landmines

- **Scaffold headers were changed once, for bugs (6 Nov 2026)**: `Slot.h`, `BoardProcessor.h`,
  `ParameterRegistry.h`, `BoardModel.h` — listed in the last `NOTES.md` entry for the owner to review. The rule
  below still stands for everything else.
- **Never call `setEffect()` or `prepare()` on a slot that is in the live chain.** Swap through
  `Slot::makeSuccessor()` (what `BoardModel::replace`/`clearSlot` do) or detach the chain first
  (`BoardProcessor::detachChain`, what `prepareAll` does). This was the worst bug the review found.
- **A pedal that goes takes its param indices with it, and the registry reuses them.** Anything holding a param
  index (mappings, macro targets, learn) must be pruned when a pedal is removed or swapped —
  `MainContent::forgetPedalParams` does it for the UI paths; `apply()` clears the mapping table before
  `model.clear()`.
- **Scaffold headers are fixed**: `IEffect.h`, `Slot.h`, `ParameterRegistry.h`, `MidiRouter.h`, `MacroBank.h`
  and the rest carry `SCAFFOLD — fixed interface`. Do not add members to them to make a card easier; work
  around them the way `ITempoAware` does, or raise it as a decision.
- **`ParameterRegistry::get()` does no bounds check.** `kInvalid` reaching it is an out-of-bounds read. Guard
  every call site (`safeGet` in `BoardProcessor.cpp`).
- **Registering a pedal's params does not bump `changeCounter()`**, so the debounced save does not see a freshly
  built board. `MainContent` marks it unsaved once at startup.
- **pump's `setSettings()` is pending until the engine's next control tick.** Leave the change pending after
  `prepare()`; clearing it changes the first milliseconds and the null test catches it at −62 dBFS.
- **`SIGTERM` does not run destructors**, so killing the app cannot test the save-on-quit path. Use the
  debounced timer save when testing persistence.
- **The test target links `PumpBoardCore` only** — no UI sources. UI is verified by render or by hand, never by
  the suite.
- **Debug-only guarantees**: the allocation hooks and the ALLOC badge exist only with `PB_ALLOCATION_GUARD`.
  A Release-only allocation would go unseen.
- **`board.xml` and `presets/` are different things**: `board.xml` is whatever you last had open, presets are
  the ones you kept. Loading a preset makes it the current board, so it is what returns next launch.
- **The working copy and the named preset are two different files.** `current.board` is the live state and
  is always written; `Boards/<name>.board` is only written by SAVE / Save As. `current.board` carries the name
  it came from, which is how SAVE knows what to overwrite after a relaunch.
- **A pending preset swap is a single slot, newest wins.** A second request inside the 25 ms fade replaces the
  pending one (before 6 Nov it ran at once and the older one then landed on top of it).
- **`board.xml` is read once for migration and never written again.** `PresetLibrary` and `presets/*.xml` are
  unused by the window now (their tests still pass). Delete one side or the other before it confuses someone.
- **`BoardData` has no tempo, and that is now settled.** The owner's answer was device-wide, so the schema is
  right as it stands and no version 2 is needed. The tempo persists across launches in
  `~/Music/pump BOARD/settings` (card 5.25); `board.xml` still carries one, but only on the migration path.
  Do not change the schema or `BoardSerializer.h` without the owner: both are the contract.
- **pump engines that voice on *change* need their settings before `prepare()`.** COIL/TWANG starts its chord only
  when `setSettings` sees a tuning change (and in `prepare`'s fresh configure), so an adapter that calls
  `prepare()` first and then applies its (equal) defaults never voices anything — that was TWANG's silence
  (card 5.31). Any future engine with the same habit needs the same order.
- **SPRING MODEL needs a re-voice, not a live redesign.** COIL's `design()` returns early for the digital models
  before snapping its delay/gain to their targets, so a live model change into a digital spring leaves the combs
  holding the previous model's delays and the voice quietly dies (card 5.34). `PumpTwangAdapter` re-voices the
  chord whenever the model changes — same order as `prepare()`, `Voice::start` designs fresh. The cost is that the
  ringing chord restarts, so you hear the new spring on the next note.
- **Latency is aligned where it is audible, and nowhere else.** TUBE's oversampler, CRUSH's 6 ms, SWIRL's
  THRU-0 3 ms and COMP's lookahead all report through `latencySamples()`, and card 5.4 now uses that number in
  both dry paths: a slot's own dry copy (so switching a pedal off fades to the same moment instead of combing)
  and the board's (so the bypass crossfade lines up). What it does **not** do is remove the chain's latency: a
  CRUSH or COMP slot still makes everything you hear arrive later — card 5.6 now shows that sum in the status
  line (`BOARD 6.0 ms`) next to the device's round trip, when it is not zero.
- **A switched-off pedal keeps its latency** (6 Nov): once its fade settles, a slot outputs its dry ring
  `latency` frames late instead of the undelayed input, so on/off never jumps the timing and the board's latency
  total is the same whatever is on.
- **The dry rings are sized from the sample rate** (6 Nov): a slot's is ceil(0.35 s) frames (never below 8192), the
  board's four times that, both allocated in prepare and clamped silently beyond it (`Slot::maxDelayFor`,
  `BoardProcessor::maxDryDelayFor`).
- **The latency probe's click bypasses the chain.** It is added to the device's output buffer by a second
  `AudioIODeviceCallback`, after the player's block, so it misses the input trim, the slots and the limiter.
  That is what makes it a clean probe; never reuse that callback for anything that should be processed.
- **A running MIDI clock owns `board/tempo`** (card 5.32). It writes at most once per beat and only when the
  value moves by half a BPM, so a steady clock is quiet; `stop` leaves the tempo where it was rather than
  rewinding it. A knob and a clock can fight — the clock wins while it runs, which is what an external
  transport means, but it is a choice the owner should confirm.
- **TUBE's `latency()` is the *current* oversampler's, not the requested one.** The engine switches with a fade
  over its next blocks, so the figure is stale until audio has run; `prepare()` primes it with 50 ms of silence
  (card 5.33). Without that, a board saved at 1x would be delay-compensated for 69 samples it no longer delays.
- **The `dynamic_cast` to `ITempoAware` runs on the audio thread**, but only on a block where the tempo
  changed. If a tempo sweep ever costs too much, cache the pointer in the chain snapshot.
- **Keep our own code at 0 warnings.** pump's headers and the scaffold's unused fields do warn; that is
  expected and left alone. Anything under `Source/` or `Tests/` should be silent, including `-Wfloat-equal`
  (compare with a tolerance).

## House rules

1. Never edit `external/pump`. Never let anything from `plugins/machine/` (GPL-3.0) into the app — not
   compiled, included, named or linked. pump's own code is all rights reserved; it is here with its author's
   say-so, for personal use.
2. The audio thread never allocates, locks or does I/O. Params are smoothed; bypass is a 10 ms equal-power
   crossfade.
3. Tests before code where the logic is testable. Log every deviation and every "not verified" in
   `docs/NOTES.md`, newest at the bottom.
4. One card per session. `docs/tasks/phase-N.md` lists the cards; they are **not** ticked off in those files —
   `NOTES.md` and this document are the record of what is done.
5. Report in plain language at the end of every task: what the app does now that it did not before, what was
   actually checked, what is still unverified, and what to try by hand.
