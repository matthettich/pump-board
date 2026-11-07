# Phase 8 — A plugin build beside the app

A second set of artefacts from the same sources: one VST3 and one Audio Unit, sharing `PumpBoardCore` with the
app, installable next to it. The standalone keeps its bundle, its data folder, its release asset and its
behaviour. Nothing in this phase is allowed to change how the app sounds, saves or starts.

The engine is already most of a plugin. `BoardProcessor` is a `juce::AudioProcessor` played through
`juce::AudioProcessorPlayer`; the DSP, board model, registry, router and serializers are in a static library
with no app code in it; `.board` JSON already carries slots, macros and mappings. What is missing is the
wrapper, the editor's freedom from `AudioEngine`, state in the host, automation, host tempo and latency, and
packaging.

**Formats:** VST3 and AU. VST2 is not available in the JUCE version this project pins and is not wanted. AUv3,
AAX and CLAP are out of scope.

**Boundary rules for every card:**

- Nothing under `external/` is edited.
- The scaffold headers (`BoardProcessor.h`, `ParameterRegistry.h`, `MidiRouter.h`, `Slot.h`) are not changed. If a
  card seems to need one, stop and say so.
- No file in `Source/` gets a plugin-only `#ifdef`. New plugin code goes in `Source/Plugin/`.
- The plugin never calls `juce::LookAndFeel::setDefaultLookAndFeel`.
- The plugin never writes `current.board` or `settings` in `~/Music/pump BOARD/`.
- The plugin does not link `Source/App/` (that is where `START_JUCE_APPLICATION` and the device owner live).

---

## Decisions for the owner, before card 8.1

1. **Automation surface for v1.** Recommended: macros `0..7`, `board/bypass`, `board/inputTrim`,
   `board/outputTrim` and the sixteen slot switches. Per-pedal knobs stay in the UI and in host state, not as
   automatable parameters. Exposing every pedal parameter means inventing stable identities for values that are
   currently indexed by a per-slot uid that changes with every insert, swap and load.
2. **Latency.** Recommended: report the chain's summed latency to the host and refuse pedal latency changes
   while audio is running, the same rule card 5.33 already applies to oversampling. The alternative is letting
   total latency move mid-play, which some hosts handle with a gap.
3. **Preset folder.** Recommended: the plugin reads and writes the same `~/Music/pump BOARD/Boards` library, so a
   board saved in the app opens in the plugin. The working copy is not shared: a plugin instance's board lives in
   the host's state chunk and nowhere else.
4. **Architecture.** Recommended: universal (`arm64;x86_64`) for the plugin only, since an arm64-only bundle
   cannot load into an Intel host. The app stays arm64 as it is today.

---

## Card 8.1 — The target exists

**Files:** `CMakeLists.txt`, `Source/Plugin/PluginEntry.cpp` (new, a stub processor).

Add `juce_add_plugin(PumpBoardPlugin ...)` with its own bundle id (`xyz.matthettich.pumpboard.plugin`), its own
manufacturer and plugin codes, `FORMATS VST3 AU`, `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT TRUE`,
`NEEDS_MIDI_OUTPUT FALSE`, `IS_MIDI_EFFECT FALSE`, `EDITOR_WANTS_KEYBOARD_FOCUS TRUE`,
`COPY_PLUGIN_AFTER_BUILD FALSE`, and a plugin category. Link `PumpBoardCore`, `PumpBoardFonts` and
`juce::juce_gui_extra`. Confirm the argument names against the JUCE commit the repo actually pins — this is the
one card where guessing an API is not acceptable.

Remove `JUCE_STANDALONE_APPLICATION=1` from `PumpBoardCore`'s `PUBLIC` defines. It is referenced nowhere in
`Source/` and no JUCE module uses it (both verified), and the app does not lose it: `juce_add_gui_app` defines it
privately on the app target itself. Left public it would follow the core into the plugin and make the shared code
believe it is an app.

The stub is an `AudioProcessor` that passes audio through, so the build and the validators have something real
to chew on before any of the app is involved.

**Check:** `cmake --build build --target PumpBoardPlugin_VST3 PumpBoardPlugin_AU` produces both bundles;
`PumpBoard` and `PumpBoardTests` still build and the Debug suite still reports 3216 expectations, 0 failed; `auval`
passes on the stub; the app's zip contents are byte-for-byte the same shape as before.

---

## Card 8.2 — Session and processor: the board plays in a host

**Files:** `Source/Plugin/PluginSession.{h,cpp}` (new), `Source/Plugin/PluginProcessor.{h,cpp}` (new),
`Source/Engine/BoardModel.{h,cpp}`, `CMakeLists.txt`.

`PluginSession` owns what must outlive the editor: `ParameterRegistry`, `MidiRouter`, `KeyRouter`,
`LearnController`, `MacroBank`, `BoardProcessor`, `BoardModel`, and a message-thread `Timer` that calls
`collectGarbage()` on the board and the router. Construction order mirrors `Main.cpp`:
registry → router → processor → model → keys/learn/macros.

The plugin gets its own `KeyRouter`, `LearnController` and `MacroBank` inside the session. The app is not touched:
`MainContent` keeps constructing its three as it does today. That split is deliberate, not an oversight — the
app's editor always exists, the plugin's may never, and the plugin's state (card 8.3) needs the session's copies
whether or not a window is open. Card 8.5 does the ownership move, when the extracted `BoardContent` has to take
these as references so one set of objects can serve both hosts. Two consequences land there with it:

- `BoardContent` sets callbacks on `LearnController` and `KeyRouter` that capture `this`. Its destructor must
  clear them, or an editor that closes while a learn is armed leaves a dangling callback.
- The skin rebuild (`buildContent`) must keep the same instances, so macros no longer reset when the skin
  changes.

`PluginProcessor` is the `AudioProcessor` the host sees: it holds the session, delegates `processBlock` to
`BoardProcessor`, accepts stereo-in/stereo-out and mono-in/stereo-out in `isBusesLayoutSupported` (a guitar on a
mono track must not be forced to stereo), returns `"pump BOARD"` from `getName`, and returns a null editor for
now. Tempo comes from `board/tempo` as it does today.

**Check:** a board of three pedals loads in a VST3 host and in an AU host and processes audio; the app still
builds, runs and passes its suite; a new `Tests/PluginSessionTests.cpp` proves the default chain, a processed
block with the allocation guard at zero, the mono fold-up, a re-prepare at a new rate and block, and destruction
with an insert still pending. The board's processor must also be told the rate and block itself: only the app's
`AudioProcessorPlayer` would normally do that.

---

## Card 8.3 — State in the host

**Files:** `Source/Plugin/PluginProcessor.cpp`, `Source/Plugin/PluginSession.cpp`,
`Tests/PluginStateTests.cpp` (new).

`getStateInformation` writes one JSON object: a version tag, the `BoardData` from `BoardState::capture`, and the
tempo the board was last playing at. `setStateInformation` applies it through the same `apply()` path a preset
load uses, on the message thread, with the board bypass fade so a restore during playback does not click. It
must tolerate empty, truncated, foreign and future-versioned data by leaving the current board alone and
publishing nothing.

`BoardState::capture` needs the macro bank and mapping list, which is why the session owns them (card 8.2) rather
than the editor: state is saved and restored whether or not a window was ever opened.

The plugin does not read or write `workingBoardFile()` and does not read `settings`. Tempo in the document comes
from the state; tempo from the host is card 8.6.

**Check:** round-trip equality of slots, per-pedal params, switches, macros and mappings through the
`AudioProcessor` API only (save into a `MemoryBlock`, restore into a fresh processor); garbage input leaves the
board intact; restoring twice in a row is idempotent; no pedal is freed while the audio thread holds it (the
retire counter is observed by the test).

---

## Card 8.4 — Automation parameters

**Files:** `Source/Plugin/PluginParameters.{h,cpp}` (new), `Source/Plugin/PluginProcessor.cpp`,
`Tests/PluginParameterTests.cpp` (new).

A parameter class that reads and writes a `ParameterRegistry` index, so the values keep a single home. Use the
decision from "before card 8.1" for the set. Each parameter has a stable string id (`macro/0`, `board/bypass`,
`slot/3/on`), a name that follows the macro's rename, and a range taken from the registry entry.

Three directions to get right, each with a test:

- host → board: `setValue` is called on the audio thread by some hosts, which is safe because
  `ParameterRegistry::set` is already any-thread and lock-free;
- UI → host: a knob writes the registry as it does today, and the parameter must then notify the host exactly
  once. Track the last value the parameter itself pushed and compare, rather than trusting a change counter;
- gesture: `beginChangeGesture`/`endChangeGesture` bracketed around a drag, and `setValueNotifyingHost` never
  called during a gesture the host started.

Macro renames and the slot list changing must call `updateHostDisplay` so a host redraws its generic editor.

**Decision (and it is a real one): automation follows the slot, not the pedal.** `slot/3/on` is a position in the
chain, and it keeps naming position 3 after a pedal is inserted before it, exactly as a fixed-lane mixer's channel
3 does. The alternative — an ID that travels with the pedal — would change the parameter list at runtime, which
hosts do not accept, and would make a saved automation lane point at a pedal that may no longer exist. The
consequence to state in the docs: inserting a pedal shifts which pedal an existing lane drives; the pedal's own
settings move with the pedal, its automation lane does not.

**Check:** automation written by the host reaches the registry; a registry write from the UI is seen by the host
once; a full automation pass allocates zero times; inserting at position 3 keeps `slot/3/on` naming position 3 (the
new pedal), while the pedal that moved keeps its settings and its own lane moves with its position.

---

## Card 8.5 — The editor, without the device

**Files:** `Source/UI/BoardContent.{h,cpp}` (new, extracted from `MainWindow.cpp`),
`Source/UI/HostServices.h` (new), `Source/App/MainWindow.cpp`, `Source/Plugin/PluginEditor.{h,cpp}` (new).

`MainContent` is a private class in an anonymous namespace that takes `AudioEngine&`. Extract it as
`BoardContent` with a small `HostServices` interface instead, and let each host fill it in:

- the standalone: device name, rate, block, reported latency, fallback and error text; the settings overlay with
  the device picker and round-trip measurement; `workingBoardFile()` as the working copy;
- the plugin: host name and rate, host-reported block size, the chain latency the plugin reports; no settings
  overlay; no working copy at all.

What must not survive the move: the global look-and-feel assumption. The app sets one process-wide in
`Main.cpp`; the plugin must apply `IdmLookAndFeel` to the editor subtree, and every `AlertWindow` and popup the
content opens must be given that look-and-feel explicitly, or the skin silently does not apply to dialogs.
`PluginEditor` is a `juce::AudioProcessorEditor` sized 1300×720 like the window, resizable with the same limits,
hosting `BoardContent` and nothing else.

The tempo field stays visible but read-only in the plugin (card 8.6 gives it the host's tempo), and the preset
buttons keep working against the shared folder. Keyboard mappings work only while the editor holds focus; a host
that eats a key wins, and that is documented rather than fought.

**Check:** the app window and its settings overlay behave exactly as before; the plugin editor opens, resizes and
closes ten times in a row under the leak detector; a dialog opened from the plugin is skinned and owned by the
editor; closing the editor during a pending bypass swap and during a preset load does not crash; the app's own
`LookAndFeel` is untouched by loading the plugin (verify in a host with a visible UI, by hand).

---

## Card 8.6 — Tempo and latency from the host

**Files:** `Source/Plugin/PluginProcessor.cpp`, `Source/Plugin/PluginSession.cpp`,
`Source/Engine/BoardModel.cpp` (only if the freeze rule needs a query the model does not already answer),
`Tests/PluginHostSyncTests.cpp` (new).

Tempo: read `getPlayHead()` once per block; when the host reports a BPM, write it to `board/tempo` and make the
header field read-only. When it does not, fall back to the field and to MIDI clock as today. Do not let two
sources fight: a host BPM always wins over a MIDI clock.

Latency: report the chain's summed `latencySamples()` through `getLatencySamples()`. When a pedal is added,
removed, swapped or switched in a way that changes the total, hold the change until the input has been silent,
reusing the rule and the mechanism card 5.33 added for oversampling. A host's delay compensation changing under
a held note is audible; a change in the gap between phrases is not.

`getTailLengthSeconds` should return the longest reverb or delay tail the board can have, not zero — a host
uses it to decide how long to keep rendering after a stop.

**Check:** the reported latency equals the sum of the chain's pedal latencies, before and after a bypass; a
latency-changing edit during tone is deferred and lands in silence; a sample-rate change re-prepares every slot
and updates the reported latency; the header field shows host BPM and cannot be edited while the host supplies
one.

---

## Card 8.7 — Programs and presets

**Files:** `Source/Plugin/PluginProcessor.cpp`, `Source/Plugin/PluginSession.cpp`,
`Tests/PluginProgramTests.cpp` (new).

Map the host's program list onto the sorted `.board` files, the same order that already defines the
program-change numbers. `setCurrentProgram` reads and applies like the `<`/`>` keys, through the bypass fade, and
leaves the working copy alone. A program whose file vanished reports its last known name and loads nothing.

MIDI program change inside the MIDI buffer keeps working through `MidiRouter` as it does now; the two paths —
host program selection and MIDI program change — must not run two loads at once, so both submit to the same
message-thread queue the content already uses.

**Check:** the program list matches the folder; selecting each program in turn lands on the right board; deleting
a file mid-session does not crash the list; a burst of program changes does not free a snapshot the audio thread
is holding.

---

## Card 8.8 — Host-shaped blocks and rates

**Files:** `Source/Plugin/PluginProcessor.cpp`, `Tests/PluginRobustnessTests.cpp` (new).

Hosts are less polite than a single CoreAudio device. Prepare and process at 44.1, 48, 88.2 and 96 kHz; blocks of
0, 1, 7, 64, 128, 511, 1024 and 4096; mono-in/stereo-out and stereo-in/stereo-out; silent, full-scale and
non-finite input. `Slot::process` already passes dry rather than overrunning when a block exceeds what it was
prepared for, and `prepareToPlay` must call `BoardModel::prepareAll()` so the prepared block is the host's
maximum, not the last device's 64.

Double-precision processing stays off.

**Check:** every combination above runs without a crash, an allocation in Debug, or a NaN leaving the processor;
the prepared-block guard is exercised deliberately and the board passes dry rather than going silent; a host
that calls `prepareToPlay` again mid-session with a smaller block re-prepares the slots.

---

## Card 8.9 — Validation, packaging and CI

**Files:** `.github/workflows/mac-build.yml`, `README.md`.

A second workflow job builds the plugin on Apple silicon and runs the AU validator and a strict third-party
plugin validator over the result. A failure there must fail the job, with the first useful line surfaced the way
the app build already does it.

Package `pump-BOARD-plugin-mac-arm64.zip` containing `pump BOARD.vst3` and `pump BOARD.component`, plus a short
install note: VST3 to `~/Library/Audio/Plug-Ins/VST3`, AU to `~/Library/Audio/Plug-Ins/Components`, and the
existing quarantine note for an unsigned build. Publish it as its own release asset; the app's `latest` release
keeps its shape.

Signing and notarisation wait for a developer account, as with the app. Note the licence position before the
first public plugin binary: the repository has no licence yet, JUCE is dual-licensed, and the VST3 format carries
its own licence terms. That is an owner decision, not an engineering one.

**Check:** CI produces both artefacts from a clean checkout; the validator passes at its strictest setting; a
fresh Mac installs by unzipping to the two folders and both formats load; the app release asset is unchanged.

---

## Not in this phase

Per-pedal automation lanes; AUv3; AAX; CLAP; MIDI output; a plugin-side MIDI device picker (the host owns MIDI);
loading `.pedal` files from a drag; two plugin instances sharing one editor; a second copy of the board's
working-copy behaviour.

---

## Acceptance for the phase

- The app: same bundle, same data folder, same tests, same release asset, same sound.
- The plugin: loads as VST3 and AU; editor opens and closes cleanly; the board, macros and mappings survive
  save, quit and reopen of the host project; macro automation drives the sound; two instances in one project hold
  two independent boards and neither writes the standalone's working copy.
- Debug builds: zero allocations in `processBlock`, with the guard proving it.
- Validation: `auval` passes, the strict plugin validator passes.
- Refused on purpose: no pedal latency change under a held note, no tempo fight between host and clock.

## Landmines

- **The global look-and-feel.** `Main.cpp` sets it for the process. A plugin that does the same restyles the
  host. Scope it to the editor, including dialogs.
- **Ownership of macros and mappings.** They live in the editor today, so state saved with the editor closed
  would silently lose them. Card 8.2 moves them; nothing later can work if it is skipped.
- **Snapshot lifetime.** Pedal chains are published and retired at N+2. The plugin must run
  `collectGarbage()` on a timer even with no editor open, and must never publish from the audio thread.
- **`prepareToPlay` while running.** Some hosts prepare again during playback. `BoardModel::detachChain` waits
  for two blocks; do not call it from the audio thread or while holding anything the audio thread needs.
- **Parameters are indices.** The registry index for a pedal's knob is not stable across a load. Anything that
  caches one across a board change is a bug, and the parameter layer is the most likely place to introduce it.
- **Two hosts, one file.** A board saved from the plugin and the same board open in the app are two copies on
  disk; the last write wins. That is acceptable for named presets and unacceptable for a working copy, which is
  why the plugin has none.

## Effort

| Card | Work | Estimate |
| --- | --- | --- |
| 8.1 | Target, stub, build plumbing | 2–4 h |
| 8.2 | Session, ownership move, first sound in a host | 1–1.5 d |
| 8.3 | State in the host | 0.5 d |
| 8.4 | Automation | 1–2 d |
| 8.5 | Editor extraction and plugin editor | 1.5–2 d |
| 8.6 | Tempo, latency, tail | 0.5–1.5 d |
| 8.7 | Programs | 0.5 d |
| 8.8 | Blocks and rates | 0.5–1 d |
| 8.9 | Validation, packaging, CI | 1 d |

That is roughly a week of focused work for something releasable, with 8.1–8.3 landing a version you can already
play through in a host.
