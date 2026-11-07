# Decision log

Append one entry per decision, deviation or open question. Newest at the bottom.

Format:

```
## YYYY-MM-DD — card N.M
- What: …
- Why: …
- Open question for review: … (optional)
```

## 2026-11-06 — card 0.1
- What: Built with Ninja (`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`) instead of the kit's `-G Xcode`.
- Why: This machine has only the Command Line Tools (26.6), not Xcode.app, so the Xcode generator is unavailable. Ninja was installed with Homebrew alongside CMake 4.4.4.
- Open question for review: if you want the Xcode generator and its debugger, install Xcode.app; otherwise Ninja is fine and faster. The README's build command should be updated either way.

## 2026-11-06 — card 0.1
- What: The build exports `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.
- Why: A macOS 27.0 SDK is present in the CLT folder and is the default, but the installed linker (ld-1267, from CLT 26.6) cannot parse its `.tbd` stubs ("unknown architecture arm64e.x1-macos"), so every link failed, including CMake's own compiler probe inside juceaide. Pinning the 26.5 SDK fixes the link. `-DCMAKE_OSX_SYSROOT` alone is not enough because juceaide configures as a nested project that drops the flag; the environment variable is inherited.
- Open question for review: this is a machine-level workaround, not a project setting. The real fix is reinstalling matching Command Line Tools (or removing the stray 27.0 SDK), after which the plain command works.

## 2026-11-06 — card 0.1
- What: `external/JUCE` is a shallow clone of JUCE master, which is currently **JUCE 9.0.5**, not JUCE 8 as the plan specifies. It is gitignored rather than added as a git submodule.
- Why: Getting card 0.1 building first; the submodule wiring can follow once the version is settled.
- Open question for review: pin `external/JUCE` to the latest 8.x tag to match the plan (API and licence terms are the ones the plan was written against), or adopt JUCE 9 deliberately. Recommend pinning to 8.x until a review says otherwise.

## 2026-11-06 — card 0.1
- What: Added `.gitignore` (`build/`, `external/`, build logs). Not named in the card.
- Why: `git init` would otherwise pick up the 126 MB JUCE clone and build output.

## 2026-11-06 — card 0.1
- What: The test binary is at `build/PumpBoardTests_artefacts/Debug/PumpBoardTests`, not the README's `build/Tests_artefacts/Debug/PumpBoardTests`.
- Why: JUCE names the artefacts folder after the target (`PumpBoardTests`), not the CMake project's Tests directory.

## 2026-11-06 — card 0.1
- What: `PedalGraph.cpp`'s stub does not touch the `out` parameter.
- Why: `PedalRenderPlan` is only forward declared in the scaffold header, so `out.reset()` fails to compile (`sizeof` on an incomplete type). Card 6.2 defines the type and may reset it there. No scaffold header was changed.

## 2026-11-06 — card 0.1
- What: Scaffold headers compiled unchanged; no fixes were needed to them (the README expected some).
- Why: Recording it so the first-time build state is known.

## 2026-11-06 — out of band: the pump style prototype (not a card)
- What: Added `Source/UI/PumpStyle.h` (a JUCE port of pump's `common/ui/Theme.hpp` palette maths, the accent table and the Simple-skin drawing primitives) and `Source/UI/BoardPrototype.h/.cpp` (16 slots, slot editor, macro strip, status line). `Main.cpp` shows the board in the window instead of card 0.1's empty shell, and takes `--render <file.png> [scale]` to draw the board offscreen for inspection.
- Why: asked to start building in pump's aesthetic ahead of the cards that would get there (2.5 board UI, 3.4 macro strip, Phase 7 skin). The prototypes are throwaway scaffolding for the look: none of the scaffold interfaces changed, no engine behaviour was added, and the slot table is a fixed stand-in for BoardModel.
- Mapping chosen (review): board chrome = `machine` silver; DRIVE = `tube` cherry, TIME = `echo` fuchsia, SPACE = `spring` gold, FILTER = `eq` aqua, DYNAMICS = `comp` pink, MOD = `swirl` rose, UTIL = `machine` silver. Each slot's neon goes on its top edge, its LED, its footswitch fill and the editor's dials when selected.
- Fonts: Share Tech Mono and JetBrains Mono are loaded at runtime from `external/pump/common/fonts` (SIL OFL 1.1) with a monospace fallback. That keeps font binaries out of the repo for now, but it makes the app depend on the submodule being present. TODO: embed them the way pump's `FontData.cpp` does before the app is packaged.
- Gotcha for later UI cards: JUCE's `Font::withExtraKerningFactor` is a fraction of the font height, not pixels (pump's `textLetterSpacing` is pixels). `style::fonts::get` divides by the height; keep using it rather than calling `withExtraKerningFactor` directly.
- Gotcha: `juce::String (const char*)` asserts on non-ASCII, so UTF-8 literals must go through `style::u8 (...)`. The first render was full of assertion boxes until this was fixed.

## 2026-11-06 — card 1.1 (DSP separability map)
- What: Wrote `docs/PUMP_DSP_MAP.md`. Every effect in pump has a framework-free `XxxEngine.hpp` beside its DPF wrapper, including only pump's `common/dsp/*` and its own `XxxParams.hpp`. Mapped all 25 effects with their engine and parameter headers.
- Result: none of them needs a refactor. The engine API is identical across the suite: `prepare(sampleRate, maxBlock)`, `setSettings(const Settings&)` at block start, `process(float* L, float* R, int n)` in place, `latency()`. Values arrive in plugin units (dB, %, ms, Hz, seconds), not 0..1.
- Compiled in: SPRING, ECHO and TUBE engines added to `PUMP_DSP_SOURCES`; `PUMP_DSP_INCLUDES` now points at `external/pump/common` plus the three plugin folders.
- Deviation: the scaffold's `PUMP_DSP_INCLUDES` was `external/pump`, which cannot resolve pump's own `#include "dsp/Fx.hpp"` / `"PumpParams.hpp"`. Changed to `common/` + the plugin folders (still CMakeLists.txt only, no header touched).
- Evidence: `Tests/PumpDspProbeTests.cpp` runs each of the three engines for 16 blocks of a 220 Hz sine at 48 kHz / 256 samples and asserts the output is finite, audible and reports its latency. All 11 tests pass.
- Finding for the chain: TUBE's `latency()` changes with its OVERSAMPLING setting (SPRING and ECHO report 0). A 16-slot series board therefore needs latency compensation, or must forbid mid-play oversampling changes. Decide before card 2.2.
- Finding: TWANG has no engine of its own — `PluginTwang` drives `pump::coil::Engine`, an instrument's engine. Building TWANG means building COIL's DSP too. Treat as its own card, not a Phase 5 copy-paste.
- Interface question for review (not worked around): `pb::ParamInfo` has no choice labels, so SPRING's `model`, ECHO's `mode`/`stereo`/`note`/`pitch_mode` and TUBE's `mode`/`focus`/`oversampling` would show numbers where pump shows names ("Short tank"). Either add an optional choices list to `ParamInfo` or let adapters supply their own editors.
- pump is pinned at `7d981aae180ff32a272ea19b09db7707b4a77006` (shallow clone, gitignored). Pin it as a submodule before this is shared.

## 2026-11-06 — build warnings after card 1.1
- What: 25 warnings appear, none in code written for this project: 15 are `-Wunused-private-field` in scaffold headers whose fields the later cards fill in (`ParameterRegistry`, `MacroBank`, `MidiRouter`, `BoardProcessor`, `Slot`), 6 are `-Wshadow-uncaptured-local` in scaffold constructors (the parameter shadows the member it initialises), and 4 are pump's own (`-Wswitch-enum`, implicit `long` -> `double`).
- Why: left alone on purpose. Scaffold headers are fixed interfaces and pump is read-only. The shadow ones are worth a cosmetic pass at a review, not a silent edit.

## 2026-11-06 — pump's licence (found while confirming the repo)
- Source: `https://github.com/matthettich/pump`, local clone level with upstream main at `7d981aae` (verified by fetch, working tree clean, shallow).
- What: pump's own code is "Copyright (c) 2026 wake. All rights reserved." — the repository being visible grants no licence to copy, modify or redistribute. That is fine here because the board ships pump code only with its author's say-so; it does mean pump BOARD cannot be given a permissive licence while it contains this source.
- Hard rule for this project: nothing from `external/pump/plugins/machine/` may be compiled in, included, named or linked from our sources. pump MACHINE is GPL-3.0-only together with the pump code built into it, and pump enforces the boundary in CI with `tools/check-licence-boundary.sh`. It is a drum machine, so nothing in the plan needs it; the rule exists so a later "nice to have" cannot quietly relicense the app.
- Third-party: the fonts we load from `common/fonts` are SIL OFL 1.1 (Share Tech Mono, JetBrains Mono), so their `OFL-*.txt` must be copied into the app bundle when fonts stop being runtime-only (already noted for packaging). pffft is BSD-style and only needed by FFT-based effects we have not pulled in.
- Not done: no licence file or notice added to pump BOARD yet — that is a packaging decision for the author, not something to choose silently.
- Decision (2026-11-06): the author is not selling pump or pump BOARD. Personal project, no commercial distribution. So no licence file, EULA or commercial notices are needed while it stays private.
- What still holds anyway, because it keys off giving copies away rather than payment: a free build shared with anyone still has to carry the SIL OFL texts for the fonts, and still must not contain pump MACHINE (GPL-3.0 obligations attach to distribution, not to sale). If it is only ever run on the author's own Mac, neither applies.

## 2026-11-06 — cards 0.2 and 0.3 (device, settings, passthrough, meters)
- What: `Source/Engine/AudioEngine.h/.cpp` (device + player + persistence + reported latency), `Source/UI/SettingsView.h/.cpp` (JUCE device selector), `Source/UI/MeterBar.h/.cpp` (in/out meters + trims), `Source/App/MainWindow.h/.cpp` (board, meter column, live status line, settings overlay), `Main.cpp` wired up, `ParameterRegistry` implemented, `BoardProcessor::prepareToPlay/processBlock` implemented, `Tests/ParameterRegistryTests.cpp` and `Tests/BoardProcessorTests.cpp`.
- Verified on this Mac: device opened at 48 kHz / 64 samples, 1 in / 2 out, no fallback; `audio.xml` written; a planted 128-sample setup came back as 128, so restore works. Reported latency on the built-in speakers is 46.8 ms at 64 samples — that is the Mac's own output path, not the board; an interface will be the real number.
- **Deviation (JUCE 9):** `AudioDeviceManager::createStateXml()` returns nothing until `lastExplicitSettings` exists, which never happens on a plain first open — so the card's "save the device setup XML" wrote no file at all. The setup is now written by us as `<PUMPSETUP type output input rate buffer inputs outputs>` into the same `~/Library/Application Support/pump BOARD/audio.xml`, and restored by opening a device and calling `setAudioDeviceSetup()`. If the saved interface is gone, the default open is kept and the status line says FALLBACK.
- Deviation: `BoardProcessor`'s constructor now passes `BusesProperties` (stereo in/out) to its base class, in the .cpp only. Without a declared bus layout `AudioProcessorPlayer::setPlayConfigDetails` cannot set the channel counts the device offers. The scaffold header is unchanged.
- Deviation: mono input is detected per block (channel 2 silent, channel 1 not) rather than "silent for 1 s". The fixed `BoardProcessor.h` has no counter member for the 1 s test; adding one needs review.
- Finding: `ParameterRegistry::get()` is inline in the fixed header and does no bounds check, so `kInvalid` reaching it is an out-of-bounds read. Guarded at every call site (`safeGet` in BoardProcessor) and inside `set`/`toggle`/`getNormalised`. Worth a bounds check in the header at review.
- Deviation: `SettingsView` offers 0..2 inputs rather than 1..2 so a laptop with no input can still be configured; fallback step 4 opens 0 in / 2 out for the same reason.
- Deviation: `BoardPrototype` gained `setStatusText()` (our file, not scaffold) so the status line shows the live device instead of the mockup's fixed string; `--render` still passes the design text explicitly. `MainWindow` moved out of `Main.cpp` as card 0.2 asks.
- Note: the trim dials' `LookAndFeel` lives inside `MeterBar.cpp`; card 1.5's generic editor will want it, so it moves to its own header then.
- Board bypass is wired (10 ms equal-power crossfade) but inaudible until card 2.2 publishes a chain — wet and dry are the same signal with no slots. The safety limiter clips at -0.3 dBFS and is always on.
- Not done: card 0.4 (device-reported latency in the settings view and status bar is live; the "Measure round trip" click probe is not) — it needs a loopback cable to verify.

## Standing preference — how to report
- 2026-11-06: end every task with a plain-language summary the owner can read without knowing the codebase: what the app does now that it did not before, what was actually checked, what is still unverified, and what to try. No file paths, no jargon, no tunings unless they are explained. Written down here so later sessions keep doing it.

## 2026-11-06 — cards 1.2 and 1.3 (catalogue and the first pump pedal)
- What: `EffectRegistry` implemented (duplicate assert, `list()` sorted by category then display name, case-insensitive); `Source/Effects/adapters/PumpSpringAdapter.h/.cpp` (pump SPRING behind `IEffect`); `BuiltInEffects.cpp` registers `pump.spring` in category "space"; `Tests/EffectRegistryTests.cpp` and `Tests/AdapterNullTests.cpp`; `Main.cpp` calls `registerBuiltInEffects` at startup.
- Null test result: adapter vs pump's own `pump::spring::Engine`, 2 s of a 440 Hz burst then seeded noise, at 64 / 128 and 511 samples, with default settings and with three params edited through `setParam` — **peak difference 0.0 (bit-identical)**. The card asked for below -90 dBFS.
- **Finding worth remembering for every later adapter:** pump's `Engine::setSettings()` only marks the change as pending; it is applied on the engine's next control tick. An engine that has already applied its settings therefore does not produce the same first milliseconds as one with a change pending. My first version cleared the pending flag in `prepare()`, and the null test caught it at -62.6 dBFS — a start-up transient difference, identical at every block size (which is what ruled out a block-size bug). Fixed by leaving the change pending after `prepare()`, and by having `reset()` do a full `prepare()` + `setSettings()` rather than `engine.recover()`, so the adapter is in exactly the state pump's engine is in after its own prepare.
- Deviation: pump's `bypass` param (kBypass) is not duplicated in the adapter's param list. The board's bypass is the slot's own on/off switch; two bypasses would fight. The adapter exposes the 13 sound params.
- Known gap (in the map too): `ParamInfo` has no choice labels, so `model` reads as 0..5 with no names. SPRING has six named models.
- Deviation: the scaffold's core glob covered `Source/Effects/*.cpp` but not `Source/Effects/adapters/*.cpp`, so adapter sources would never have been compiled. Added to `PB_CORE_SOURCES`.
- Build warnings rose to 30, all from pump's own headers being compiled under our warning flags; none in `Source/` or `Tests/`.
- Not yet audible: the pedal is registered in the catalogue but nothing puts it in a slot, so the board still passes audio untouched. Cards 2.1 (Slot) and 2.2 (BoardModel + snapshot publishing) are what make it heard.

## 2026-11-06 — cards 2.1 and 2.2 (slot and board: the reverb becomes audible)
- What: `Slot.cpp` implemented (registers `slot/<uid>/on` in the ctor and one id per effect param in `setEffect`, 10 ms equal-power crossfade, skips the effect entirely when fully bypassed — card 2.1's v1 rule, so tails stop with the switch); `Source/Engine/BoardModel.h/.cpp` (insert / replace / remove / move / clear, each publishing a `ChainSnapshot`); `BoardProcessor::publish` / `collectGarbage` (retires at block N+2) and a destructor that frees what is left; `MainWindow` drives garbage collection at 30 Hz and mirrors real slot state onto the board; `BoardPrototype` footswitches are now clickable and its slot 1 shows the real pedal; `Main.cpp` loads `pump.spring` into slot 1 at launch and logs it; `Tests/SlotTests.cpp`, `Tests/BoardModelTests.cpp`.
- Verified: 111 tests pass, 0 failing. The switch test measures the largest sample-to-sample step across an on→off transition and requires it under 0.05 (the card's threshold) for a DC input — it passes, and it caught a bug in my own test first (I was measuring from a settled wet signal, so the first sample looked like a 0.25 jump). `BoardModelTests` proves a published slot is actually heard on the audio path (a gain-2 effect turns 0.25 into 0.5 through `processBlock`) and that clearing the board returns the signal to dry. The app launched, published 1 slot, stayed alive with the chain live and left no crash report.
- Design note: `BoardProcessor::processBlock` now applies the output trim to *both* sides of the board-bypass crossfade. It used to apply only to the wet path, so bypassing the board with a non-zero output trim would have changed the level.
- Design note: a device format change rebuilds the slots (new `Slot` objects, values and switch state carried across, new snapshot published) rather than calling `Slot::prepare` on live slots. `prepare` allocates and calls `effect->prepare`, so doing it in place while the audio thread processes the same slot would be a data race; rebuilding retires the old slots safely.
- Deviation: card 2.2's test asked for destructor counting via a test hook, and the fixed `BoardProcessor.h`/`ChainSnapshot.h` have no such hook. The retire path is verified by behaviour instead (published → heard, cleared → dry, no crash, no leak report), which tests what the card cares about rather than an internal counter.
- Note: `BoardPrototype`'s clickable footswitches and `setSlotState` are a deliberate preview of card 2.5. Slots 2–16 still show the mockup's invented pedals; only slots that actually exist are driven, so slot 1 now reads pump SPRING.
- Not done: no knob editor for the pedal yet (card 1.5), so the sound is whatever the defaults are: mix 30%, decay 2.5 s, two-spring tank.

## 2026-11-06 — card 1.5 (a real knob panel for whichever pedal is selected)
- What: `Source/UI/GenericEffectEditor.h/.cpp` — one control per `ParamInfo` of the selected slot's effect: rotary for values, a neon key box for toggles, name above and value below, double-click returns to the default. Values go through `ParameterRegistry` only, and a 30 Hz timer pulls them back so the panel cannot drift from the engine. `Source/UI/PumpDialLookAndFeel.h` — pump's dial lifted out of `MeterBar.cpp` into a header so the meters and the pedal knobs are the same knob (the comment in MeterBar said it would move here). `BoardPrototype` gained `editorBounds()` and `setPaintedEditorVisible()`, and a click on a slot body selects it (`onSlotSelected`) instead of only the footswitch toggling it. `MainWindow` puts the editor where the painted placeholder was; `Main.cpp` gained `--render-editor <png> [scale]` so the panel can be looked at without a device, and now loads the pedal before the window so the panel opens on a real effect.
- Verified: build clean, 0 warnings, 111 tests pass. Rendered `--render-editor` at 2x and looked at it: 13 knobs for the spring (model, dwell, decay, tension, drip, tone, low cut, pre-delay, wobble, width, grit, mix, output gain), values reading 50 %, 2.50 s, 100 Hz, 0 dB. App launched with the panel live, stayed up, no crash report.
- Fixed while looking: knobs printed interval-snapping dust as `2.08167e-15 %` (a 0.01 step from a −100 floor does not land exactly on 0), and the first pedal's silk still read the mockup's `DRIVE 0.62 / MIX 100%` under a `pump SPRING` title. Values now go through one `formatParamValue()` that snaps dust and picks decimals, and a slot's silk lines are its first two real params, so the body cannot claim something the pedal is not doing.
- Also: knobs honour `ParamInfo::skewCentre`, so log-scaled params (decay, low cut) put pump's geometric midpoint at the knob's midpoint.
- Deviation: card 1.5's checks are visual ("shows its knobs", "turning them changes the sound"). The knobs are verified by render and by construction (each one writes the registry index the slot gave it); the tests cannot see the UI because the test target links `PumpBoardCore` and the UI sources live in the app target. Hearing that a turned knob changes the sound is still a human check.
- Note: the painted "7 PEDALS" in the board header is still mockup text while the status line truthfully says "1 PEDAL". Slots 2–16 remain fiction.

## 2026-11-06 — card 1.4 (adapters two and three: pump ECHO and pump TUBE)
- What: `Source/Effects/adapters/PumpEchoAdapter.h/.cpp` (24 params) and `PumpTubeAdapter.h/.cpp` (14 params), both translated symbol-for-symbol from pump's `EchoParams.hpp` / `TubeParams.hpp`; registered in `BuiltInEffects.cpp` as `pump.echo` ("time") and `pump.tube` ("drive"); `Tests/EchoTubeNullTests.cpp`. The app now loads three pedals in chain order — tube, echo, spring — so slots 1–3 are real and the chain is audible in the order a guitar board wants (drive → delay → space). `--render-editor <png> [scale] [typeId]` renders any pedal's panel.
- Verified: build clean, 0 warnings in our code, 148 tests pass (including a chain test that proves three slots in a row all process: three gain-2 pedals turn 0.1 into 0.8). Echo and tube null tests are **exactly 0.0 difference** (bit-identical) at 64, 128 and 511 samples, both at defaults and edited (tape echo: mode/sync/time/feedback/mix; tube crunch: drive 24 dB, 2 stages, mix 60 %). App launched with three pedals, logged "3 slot(s) published", stayed up, no crash report.
- Note: pump's echo names its routing param `stereo` and calls it `route` in `Settings`; the adapter maps the name, the layout keeps pump's id. Echo's synced times need a tempo, which a standalone has no host for: the adapter owns a bpm (120 default) and `setTempo()`, applied in `prepare()` and on change.
- Note: TUBE reports real latency (its oversampler; 0 at 1x, most at 8x) and that changes when OVERSAMPLING changes at runtime. The board does not yet compensate latency across slots, so a tube in the chain delays everything after it by its oversampler's latency — inaudible alone, but card 2.3 (limiter) and the latency card must handle it. `latencySamples()` reports the truth rather than 0.
- Fixed while looking: the editor laid out 7 knobs per row, which put ECHO's 24 params in four rows and overlapped "Mod Rate" with the Freeze key. Now 12 per row (two rows for echo, one plus a centred remainder for tube and spring), and integer params up to a span of 24 show as integers, so note divisions read 11 and 12 rather than 11.00.
- Deviation: `ParamInfo` still has no choice labels, so ECHO's `mode`/`stereo`/`note_*`/`pitch_mode` and TUBE's `mode`/`focus`/`oversampling` show numbers where pump shows "Tape", "Ping-Pong", "1/4D", "Triode". That is the scaffold-header question already written up in PUMP_DSP_MAP.md, not something to work around in the adapter.
- Deviation: the editor's toggle keys wear the app's own accent, not the selected pedal's category colour, because a `Slot` does not carry its category (the `BoardModel` does). Cosmetic only.

## 2026-11-06 — card 2.5, first half (rearranging and choosing pedals)
- What: `BoardPrototype` now carries a pedal: press on a filled slot, drag past 6 px, and the slot under the pointer is outlined; release to move. The carried pedal is ghosted in its old place until it lands. Right-click opens the pedal list (one submenu per category, from `EffectRegistry::list()`), plus Remove when the slot holds something. Clicking an empty slot's "+" opens the same list, and a chosen pedal is `replace`d into a filled slot or `insert`ed into an empty one. `MainWindow` wires `onSlotMoved` → `BoardModel::move`, `onSlotMenu` → the picker, and re-syncs the painted slots and the knob panel after every change.
- Verified: build clean, 0 warnings in our code, 153 tests pass, app runs with three pedals and the picker wired in. New test: replacing a slot keeps the slot's uid but takes the new pedal's defaults — a gain-2 pedal replaced by a gain-3 pedal turns 0.1 into 0.3 through `processBlock`, so the old pedal's values do not leak into the new one.
- Not verified: the drag and the menu themselves. There is no GUI harness in this project, so their behaviour is argued from the code, not exercised; `BoardModel::move` (same slot object, same uid) is the part under test. Dragging is a check for the human.
- Deviation from card 2.5: the card calls for separate `BoardView`/`SlotView`/`EffectPicker` classes with a searchable picker. The board is still `BoardPrototype` (a painted mockup) and the picker is a `PopupMenu` built from the registry, which is honest and small; splitting the prototype into real views is still owed before the board can show 16 independent pedals. No search box yet — three pedals do not need one.
- Note: `move` is an insert-at-target move (the pedal lands where it was dropped and everything between shifts), not a swap. That is what a pedalboard wants.

## 2026-11-06 — presets, first half (the board survives a restart)
- What: `Source/Presets/BoardPreset.h/.cpp` — the board as `<PUMPBOARD version="1">`: one `<slot effect="pump.echo" on="1.0">` per pedal in chain order, then a `<param id="..." value="..."/>` per control in pump's own units. `boardFromXml` clears the model and rebuilds it in file order; unknown pedal types are skipped rather than failing the board. `MainWindow` restores `board.xml` at startup (a saved board wins over the chain `Main.cpp` builds), writes it out 800 ms after the board stops changing (debounced, because a knob drag bumps the registry counter on every move), and again on quit. `Tests/BoardPresetTests.cpp`.
- Verified: build clean, 0 warnings in our code, 180 tests pass. Round trip through a real file: three pedals saved and reloaded keep their order, their types, a tube drive moved to 27 dB, an echo feedback moved to 61 %, the echo still switched off, and an untouched default (spring mix 30 %) still 30 %. A missing file leaves the board alone; a foreign XML file is refused rather than half-loaded. End to end in the app: launch 1 wrote `board.xml` (3 slots, 51 params = 14 + 24 + 13) beside `audio.xml`, launch 2 logged "restored 3 pedal(s)".
- Fixed while checking: the file first landed in `~/Library/pump BOARD/` because `File::userApplicationDataDirectory` is `~/Library` on macOS. The path is now built by hand to match `AudioEngine::getSetupFile()` (`~/Library/Application Support/pump BOARD/`), and the stray folder was removed.
- Fixed while checking: a freshly built board was never written, because registering a pedal's params does not bump the registry's change counter, so the debounced save saw "no changes". Startup now marks the board unsaved once.
- Not verified: that a knob turned in the running app reaches the file. The debounce watches the registry counter, which every `set()` bumps, and the value-level round trip is covered by the test; the GUI path (turn knob → wait → quit → relaunch) is a check for the human.
- Deviation: preset *slots* — several named boards you can browse and switch between — are not built. There is one board, the one you left behind. The file format and the read/write entry points are the foundation the browser sits on; nothing in the format needs to change for it.
- Note: the board is written on the message thread while audio runs. It is a small file and never touches the audio thread, but a save during playback is not free; if it ever shows up as a dropout, the write should move to a background thread.

## 2026-11-06 — presets, second half (named boards you can switch between)
- What: `Source/Presets/PresetLibrary.h/.cpp` — a folder of named boards (`~/Library/Application Support/pump BOARD/presets/*.xml`): `list()` sorted by name, `fileFor()` through `File::createLegalFileName` so a typed name can never point outside the folder, `save()` on top of `BoardPreset`. A `PRESETS` button sits above `SETTINGS` in the right column: it lists every saved board, loads one on click, and offers "SAVE THIS BOARD AS..." which asks for a name. Loading and saving both confirm in the status line for four seconds ("LOADED MY CLEAN TONE"), because a file operation is otherwise invisible. `Tests/PresetLibraryTests.cpp`.
- Verified: build clean, 0 warnings in our code, 192 tests pass. The library test saves a named board into a temp folder, sees it listed, loads it back and checks the pedal that came back; an absent folder lists nothing instead of crashing; an empty name is refused; and `../../../live / take 2` resolves to a file whose parent is still the presets folder, so a typed name cannot escape it. App launches with the button and the name prompt wired in.
- Not verified: the button, the menu, the name dialog and the four-second confirmation. Same limitation as the drag and the slot menu — there is no GUI harness here — so those are human checks. The file side underneath them is tested.
- Not done: presets cannot be deleted or renamed from inside the app (delete the file in the folder), and there is no "revert to the board I had before loading".
- Note: the auto-saved board (`board.xml`) and the named presets are separate: `board.xml` is whatever you last had open, presets are the ones you chose to keep. Loading a preset makes it the current board, so it is what comes back on the next launch.

## 2026-11-06 — card 2.3 (the safety limiter replaces the hard clip)
- What: `Source/Engine/SafetyLimiter.h` — peak, stereo-linked, no look-ahead, instant attack, 50 ms release, ceiling −0.3 dBFS. The sample that goes over the ceiling is already reduced when it is written, so the output cannot overshoot; the multiply result is clamped to the ceiling so that is an invariant rather than a hope. A non-finite sample is zeroed and the gain reset instead of poisoning the rest of the board. `BoardProcessor` owns one, prepared with the device rate, and runs it as the last stage after the bypass crossfade; the old `jlimit` clip and its `kCeilingDb` are gone. `Tests/SafetyLimiterTests.cpp`.
- Verified: build clean, 0 warnings in our code, 286 expectations pass. A signal under the ceiling comes out bit-identical (difference exactly 0.0) with the gain still 1. A 2.0-amplitude sine comes out at the ceiling. After a burst the gain ducks and recovers to unity within a second of quiet (50 ms release). A loud left channel ducks a quiet right one by the same gain (stereo link, checked numerically). A NaN is zeroed. Mono works with `right == nullptr`. The card's own check: **sixteen +12 dB pedals fed 0.25 peak at exactly −0.30 dBFS**.
- Note: no look-ahead means a transient is shaped rather than turned down before it arrives. That is the deliberate trade (a board of pedals, not a mastering chain); if it ever sounds like it grabs, the fix is a look-ahead buffer and a stated latency, not a lower ceiling.
- Not verified: how it sounds. Ducking a whole board for 50 ms is audible on dense playing, and whether that is pleasant is a human judgement. Also not measured: how much distortion the instant attack adds on percussive material.
- Note: with a tube in the chain its oversampler latency still shifts everything, limiter included; the limiter limits what reaches the output, not what reaches the ears late.

## 2026-11-06 — card 2.4 (the audio thread is proven not to allocate)
- What: `Source/Common/AllocationGuard.h/.cpp` — a `thread_local` depth raised by `pb::alloc::Scope`, and, in Debug builds only (`PB_ALLOCATION_GUARD`, already in CMake), replacements for global `operator new`/`new[]`/`delete`/`delete[]` including the sized and aligned (C++17) forms. Each hook counts when a Scope is open and then does exactly what the default allocator did (`malloc`/`posix_memalign`/`free`), so behaviour is identical apart from the count. `BoardProcessor::processBlock` opens a Scope as its first act. `MainWindow` appends "ALLOC n" to the status line in Debug when the count is nonzero. `Tests/AllocationGuardTests.cpp`.
- Verified: build clean, 0 warnings in our code, 307 expectations pass. Allocating inside a Scope counts; allocating outside does not. **A 16-slot board cycling spring, echo and tube through `processBlock` counted 0 allocations on every block measured, including the first two** (warm-up included, so there is no one-off allocation hiding on the first block).
- Deviation: card 2.4 asks for a red "ALLOC n" badge; it is appended to the existing status line instead, which is text-coloured. Cosmetic, and it only appears in Debug.
- Note: the hooks are Debug-only by design — in Release nothing is replaced, so the rule costs nothing where the app ships, but it also is not policed there. Any allocation that appears only in a Release binary (LTO, inlining differences) would go unseen.
- Note: `posix_memalign` is the aligned-allocation route, so these hooks are macOS/POSIX. Porting to Windows would need `_aligned_malloc`/`_aligned_free` in the same file.
- Note: the guard counts the *audio thread's* allocations only while `processBlock` runs. `prepareToPlay`, `publish`, `collectGarbage` and the whole message thread are free to allocate, as the kit intends.

## 2026-11-06 — card 2.5, second half (sixteen real slot views)
- What: `Source/UI/SlotView.h/.cpp` — one slot as a component. It paints the pedal (category neon edge, name, category, two mono values, LED, ON/OFF footswitch key) and reports toggle, select, right-click menu, drag and drop; it holds no authoritative state. `accentForCategory` moved into `PumpStyle.h` so the painted prototype and the real views share one definition. `BoardPrototype` gained `getSlotBounds(i)` and `setPaintedSlotsVisible(false)`; `MainWindow` owns sixteen views placed on the board's slot positions, resolves a drag against the whole board, outlines the drop target, and moves the pedal on release. `--render-slots slots.png [scale]` renders the board without audio.
- Verified: build clean, 0 warnings in our code, 307 expectations pass. The render shows the three real pedals and thirteen empty slots exactly where the painted ones sat, selection on slot one, categories in their own neon. The app launches, restores three pedals, and is still running seven seconds later with the new children in place.
- Not verified: the interactions themselves. Toggle, select, right-click menu, drag, drop outline and the move they trigger are wired but untested by automation (the test target links only the core; UI is app-only), and a render is a still image. A human has to click them.
- Note: the painted header still reads "7 PEDALS" whatever is on the board. Next to a board that now shows its real slots that is a visible lie; wire it to the model count.
- Note: the prototype's own slot hit-testing and drag code is now unreachable in the app — the views cover exactly the same rectangles and eat the clicks. Delete it when the painted fallback is retired.
- Note: `--render-slots` still draws the prototype's painted editor placeholder, which invents knob names under whatever pedal name it is given. The app hides it and uses the real editor; do not read that panel in a render as real.

## 2026-11-06 — the header counts the board (the "7 PEDALS" lie)
- What: `BoardPrototype::setPedalCount(int)`, painted into the header as `CLEAN TO CHAOS · N PEDALS` (singular at one, "NO PEDALS" at zero). `MainWindow::syncSlots` feeds it `model.size()`. The status line's duplicate count is gone, so the number appears once on screen. The render modes set it explicitly: 7 for the painted mockup, 1 for the editor preview, 3 for the slot render. `--render-editor` also hides the painted mock pedals now, so it shows one real pedal, fifteen empty slots and the real editor — the app rather than the mockup.
- Verified: renders read "3 PEDALS" and "1 PEDAL" in the header, and the status line no longer repeats it.
- Note: the header's `< > AB SAVE ? BYPASS` keys are still painted and still do nothing. Next to working slots they are the last pure fiction on the board.

## 2026-11-06 — the header keys (four real, two removed)
- What: `BoardPrototype` lays its header keys out from one table and hit-tests them, firing `onHeaderKey (kPrevPreset | kNextPreset | kSave | kBypass)`. `MainWindow`: `<` and `>` step through the saved boards and wrap at either end (`stepPreset`), `SAVE` overwrites the board you are on (`quickSave`) and falls back to the name prompt when nothing is loaded, `BYPASS` toggles the real `board/bypass` parameter and the key lights red while the board is bypassed. `AB` and `?` are gone — there is no second board to switch to and no help screen. `loadPresetFile` is now the single path a preset takes, so the menu, the keys and the name in the status line cannot disagree. The status line's right-hand "CMD? FOR THE FULL LIST · DARK" hint, which promised two things that do not exist, is replaced by the loaded board's name (or "NO BOARD LOADED").
- Verified: build clean, 0 warnings in our code, 307 expectations pass. The renders show `<` `>` `SAVE` `BYPASS` beside the pedal count, with no help hint and "NO BOARD LOADED" bottom right. The app launches and stays up with the new callbacks.
- Not verified: the clicks. Nothing automated can press a native header key, and I did not press them by hand either: preset stepping, quick save, and the red bypass light are unexercised. The bypass reaches the audio through the same `board/bypass` param the processor already crossfades (card 2.2), so the audio side is covered; the buttons are not.
- Note: `currentPreset` is memory only. After a quit, `<` and `>` forget where you were and start from the first saved board.

## 2026-11-06 — tempo
- What: `board/tempo` (20–300 BPM, default 120) is a board parameter owned by `BoardProcessor`. On a change — and only on a change — `processBlock` hands it to every pedal that implements the new `ITempoAware` (`Effects/TempoAware.h`; `IEffect` is a scaffold and was not touched). `PumpEchoAdapter` implements it, so its synced note times follow the board. The header has a real `TempoField`: drag up and down (3 px per beat), or double-click to type a value. The tempo is written into the board file as a root attribute and comes back with it; a file written before this change leaves the current tempo alone rather than resetting it.
- Verified: build clean, 0 warnings in our code, 575 expectations pass (up from 307). `TempoTests` proves the default and its range, and that a tempo change reaches the echo: 120 → 140, then clamped to 20 and 300 for out-of-range values, with a spring in the same board untouched and the output finite. The round-trip test now saves at 137 BPM and expects 137 back. The render shows "140 BPM" in the header beside the pedal count.
- Not verified: dragging and typing in the field. Native UI has no automated test here; the drag maths (3 px per beat, up = faster) and the typed-entry dialog have not been exercised. Nobody has listened to a synced delay follow a tempo change either.
- Note: the `dynamic_cast` to `ITempoAware` runs on the audio thread, but only on a block where the tempo changed, never per block. If a tempo sweep ever makes that too expensive, cache the pointer in the chain snapshot instead.
- Note: tempo has no other input yet — no tap, no MIDI clock, no macro. The header field is the only way to set it.

## 2026-11-06 — handover document
- What: `docs/HANDOVER.md` — build/test/run commands, what works, what is wired but has never been pressed by a human, six open decisions, the next four cards in order (3.1 MidiRouter, 3.2 keys, 3.3 learn UI, 3.4 MacroBank and the macro strip), the small jobs worth doing, and a landmine list.
- Why: this is a good place to stop before phase 3, and the traps are expensive to rediscover: scaffold headers are fixed, `ParameterRegistry::get()` does not bounds-check, pump's `setSettings()` is pending until the next tick, SIGTERM skips destructors, the test target has no UI, and the allocation guard only exists in Debug.
- Note: it restates `NOTES.md` rather than replacing it. NOTES stays the running reason log; HANDOVER is the starting point for whoever picks this up.
- Note: the cards are not ticked off in `docs/tasks/phase-N.md`, so that document plus NOTES are the only record of what is done. Ticking them would be worth doing if the plan is still being followed card by card.

## 2026-11-07 — card 3.1 (MidiRouter core)
- What: `Source/Control/MidiRouter.cpp` implemented — `publish` / `collectGarbage` retire a replaced `MappingTable` at N+2 like `BoardProcessor`, `beginLearn` / `cancelLearn`, and `handle` builds a `MappingSource` per note / CC / program change and applies every matching row. `Tests/MidiRouterTests.cpp`: thirteen tests, one per action plus the channel filter, learn, the FIFO and the retire path. `handleKey` is still `// TODO card 3.2` as the card says.
- Verified: build clean, 0 warnings in our code, **894 expectations pass** (up from 575, 13 new MidiRouter tests). Every action is checked through `handle` with real `MidiBuffer`s: Toggle flips on a note-on and not on a note-off or a note-on at velocity 0; Momentary holds the mapping's max while down and its min on release (0.9 / 0.2 on a −10..10 param, so the mapping's range is proved to be used rather than 0/1); CCThreshold at 64 / 63 / 127 / 0; SetValue at CC 0 → −10, 127 → 10, 64 → 0.0787; channel 0 matches any channel while a numbered channel ignores others; two mappings on one note both fire; the three preset actions post events and never write a parameter; a full FIFO keeps 255 events, drops the rest and blocks nothing; a swapped table is read instead of the old one and the retired one is freed only after two blocks.
- **Deviation (fixed header, the important one):** `MidiRouter.h` has no block counter and no retire list, and adding members is forbidden. Both live in a file-local table inside the `.cpp`, one slot per live router, claimed at construction and released at destruction; the audio thread finds its slot with a handful of atomic loads (never a lock or an allocation). The table holds eight routers at once and asserts in Debug beyond that — plenty, since the app owns one. Nothing in the header changed. Alternatives (a map guarded by a mutex) would have broken the "audio thread never locks" rule.
- Finding: `juce::AbstractFifo` can hold `capacity − 1` items, so the header's 256-slot `events` array queues 255. Not a bug, but a card 3.3 author expecting 256 events will be surprised.
- Deviation: `Toggle` fires only from a note-on or a key press, exactly as the header comment says. A CC assigned Toggle therefore does nothing — with no per-mapping edge state there is nowhere to remember which side of 64 the last CC value was, and flipping on every value ≥ 64 would double-flip on a held controller. `CCThreshold` is the right action for a CC switch. `Momentary` works from a note, a key or a CC (≥ 64 = held); `CCThreshold` and `SetValue` read a CC value and ignore notes.
- Deviation: preset actions fire on a "press" — note-on, key down, CC ≥ 64, any program change. A *continuous* CC mapped to PresetNext posts one event per message at or above 64, so sweeping a knob through it steps several presets. A footswitch sends one message per press, which is the case this is for; an edge detector needs state the fixed header cannot hold.
- Note: learn captures the first note-on, CC or program change and routes **nothing** while learning — not even a note-off, so a stray release during learn cannot toggle a switch. A note-off does not consume the capture. `cancelLearn` (card 3.3's Esc) puts it straight back to routing.
- Note: `handle` returns immediately when no table is published and learn is off, which is the app's state today: nothing builds a `MappingTable` yet, so MIDI is inert and `MidiRouter` allocates nothing per block whether mapped or not.
- Not verified: any of it against real hardware. There is no MIDI device in the test target and no table is published in the app until card 3.2/3.3, so a footswitch driving a slot is unexercised by both automation and a human.
- Not done: `collectGarbage` is not called from `MainWindow`'s 30 Hz timer yet, because nothing publishes a table to retire. Wire it when the first learn card does.

## 2026-11-07 — card 3.2 (keys)
- What: `Source/Control/KeyRouter.h/.cpp` (new) — computer keys as a mapping source through the same table; `MidiRouter::handleKey` implemented (it was `// TODO card 3.2`); `MainContent` forwards `keyPressed` / `keyStateChanged`, grabs keyboard focus at startup and ignores keys while a text field has focus; preset actions from keys go straight to the preset manager (`stepPreset`, plus a new `selectPreset`). `Tests/KeyRouterTests.cpp`: eight tests.
- Verified: build clean, 0 warnings in our code, **940 expectations pass** (up from 894). A key mapped to Toggle flips a slot's switch and a held key flips it once, not once per OS auto-repeat; Momentary holds the switch on only while its key is down and returns it off on release; a key that is still physically down stays held across a keyboard poll and releases when it is not; preset events come back through the callback and the audio FIFO stays empty; learn captures one key and routes nothing meanwhile; cancelLearn restores routing; a release or a bare modifier is never taken as a source; CCThreshold and SetValue do nothing from a key; an unmapped key is not swallowed.
- Design: `KeyRouter` is deliberately GUI-free (plain `int` key codes, no `juce::KeyPress`), so the suite can drive it. `MainContent` converts `KeyPress` to a code and supplies the "is this key still down" poll (`KeyPress::isKeyCurrentlyDown`), because `keyStateChanged()` does not say which key changed. `MainContent::keyPressed` is the one place that knows about focus, so the text-field guard lives there.
- Deviation: the press that is captured during learn still counts as held, so that key has to be released before it will route again. A real keyboard sends the release; the tracker simply has no notion that learn consumed the press. Found by the first version of the test, which pressed the same key twice without a release and watched Nothing happen — the router was right, the test was wrong.
- Deviation: a bare modifier change arrives with key code 0 and is ignored, so it can never be a mapping's source or toggle a switch.
- Note: keys carry no channel, so a Key mapping's `channel` field is ignored (`number` is the key code).
- Note: `PresetSelect` from a key has no program number to carry, so the event's `value` is 0 and `selectPreset` loads `files[0]`, the first saved board. Saves and the preset menu are 0-based positions into the sorted list, so a *program change* 3 loads the fourth board. Open question for the owner: if hardware is going to send program numbers, should that be 1-based in the UI? One line to change.
- Note: MIDI preset events still are not drained in the app — nothing publishes a mapping table yet, so the router's FIFO has no consumer and no producer either. Keys bypass it by design, because it has exactly one producer (the audio thread).
- Not verified: pressing anything by hand. The window's focus policy, the text-field guard and `KeyPress::isKeyCurrentlyDown` under a real keyboard are argued from the code and from JUCE's contract, not exercised. Nothing can press a key in the test target, and no mapping exists in the app until card 3.3's Learn UI publishes one.
- Not done: `MidiRouter::collectGarbage` still is not called from the window timer, for the same reason as card 3.1 (nothing publishes a table to retire).
- Finding worth keeping: `juce::Component::keyStateChanged` returns `bool`, not `void` (the first build says so).

## 2026-11-07 — card 3.3 (learn and the mapping list)
- What: `Source/Control/LearnController.h/.cpp` (the learn handshake, the mapping list and one table publish per change); `Source/UI/MappingsView.h/.cpp` (an overlay listing source, action, target and range, where clicking a row changes its action, edits its range or deletes it); `GenericEffectEditor`'s knobs and switches now answer a right-click with Reset / Type a value / Learn and outline the armed control with LEARN; the slot menu gained "MAP THIS SWITCH..."; `MainContent` gained a MAPPINGS button, drains the router's events at 30 Hz, retires replaced mapping tables and lets Esc cancel learn. `Tests/LearnControllerTests.cpp`: eight tests.
- Verified: build clean, 0 warnings in our code, **999 expectations pass** (up from 940). Proven at the controller level with the same handshake the window uses: a note learned onto a slot switch flips it and only on that channel; a key learned onto the same switch holds it while pressed; a switch defaults to Toggle and a knob to Value, and a Value mapping writes across the knob's own range; the same source onto the same target replaces the row (so one footswitch flips once, not twice) while the same source onto a second target is allowed; dismissing the action popup leaves nothing mapped; Esc disarms both sources, and whichever source wins disarms the other; a range edit and an action change both reach the router; deleting the last row leaves the table empty. The app launches with the new wiring.
- **Deviation (file location):** the card names `Source/UI/LearnController`; it is in `Source/Control` instead. The test target links `PumpBoardCore` only, so a controller under `Source/UI` could not be tested — and the card's own checks (learn from a note, learn from a key, mappings show) are exactly what the suite now proves. It includes no GUI class.
- Design: the controller owns half the handshake — it hooks `KeyRouter::onLearnCaptured` in its constructor and clears it in its destructor — so the window cannot forget to connect it. MIDI captures still arrive through the router's FIFO, drained by the window's timer, because the audio thread is that queue's only producer.
- Decision: learning the same source onto the same target **replaces** that row rather than adding a second one. Two identical Toggles on one footswitch would cancel each other out. One source driving two different targets is allowed on purpose (one switch, two pedals).
- Decision: a new mapping takes the target's own min/max as its range; the list view can narrow it afterwards.
- Decision: the action popup offers Toggle / Momentary / Threshold (CC 64) / Value with the useful one already ticked — Toggle for a switch, Value for a knob.
- Decision: Esc only cancels when learn is armed or a captured source is waiting for its action, so it is left alone otherwise.
- Deviation: "Type a value" is disabled for switches (a switch has two states, so Reset and Learn are the meaningful items), which makes the menu Reset / Type / Learn for knobs and Reset / Learn for switches.
- Note: the slot footswitch is mappable from its own right-click menu, but the editor cannot outline it because it is not one of the editor's controls; the status line names what is armed instead.
- **Not persisted:** mappings live in memory only. The board and tempo still save; every mapping is forgotten on relaunch. Phase 4's serializer is where they belong (its card already writes mapping targets by slot position).
- Finding: the first run of the new tests failed 7 expectations because only the window connected `KeyRouter`'s learn callback — the controller received MIDI captures but never key ones. Fixing it by moving that wiring into the controller removed the duplication and the dangling-hook risk in one go.
- Not verified: every click in it. The overlay, the row menu, the range and type dialogs, the LEARN outline, the MAPPINGS button and the slot-switch item are unexercised — nothing can click in the test target, and the app's own window was only launched, not driven.

## 2026-11-07 — card 3.4 (macros)
- What: `Source/Control/MacroBank.cpp` implemented — registers `macro/0..7`, keeps a name and a target list per macro, maps through the four curves, and writes a macro's targets when its knob moves. `Source/UI/MacroStrip.h/.cpp` (new) — eight real knobs where the painted strip was, each with its name, its target count, a right-click menu (Add target / Rename / Clear targets) and a target popup (low, high, shape, with both ends starting at the control's current value). `GenericEffectEditor` gained a pick-target mode so the next control clicked becomes the macro's target; `BoardPrototype` gained `macroBounds()` and can hide the painted strip; `MainContent` runs `macros.update()` at 30 Hz. Both offscreen render modes now draw the real strip. `Tests/MacroBankTests.cpp`: five tests.
- Verified: build clean, 0 warnings in our code, **1069 expectations pass** (up from 999). Curves at 0 / 0.5 / 1 across a 10..20 range: linear 10 / 15 / 20, exponential 10 / 12.5 / 20, logarithmic 10 / 17.07 / 20, inverted 20 / 15 / 10, with out-of-range input clamped rather than extrapolated. Eight macros register as `macro/<n>` with a 0..1 range and a 0 default. A macro with two targets writes both, each through its own curve; it writes **only when it moved**, so a target set by hand stays where it was put until the knob is turned again; adding a target lands it where the macro already is; names trim and fall back to "MACRO n"; out-of-range macros are harmless. Rendered `--render-slots` at 2x and looked at it: eight MACRO 1..8 dials in the real panel with the painted names gone.
- Decisions: a macro starts at 0 so it writes nothing until pulled; `setTargets` applies immediately so a new target does not sit at a stale value; `update()` skips a macro whose value did not change; a target with an invalid index is skipped rather than written.
- Deviation: the name is edited through a dialog (right-click → Rename), not in place. The card says "editable name" without saying where, and a dialog keeps the strip from needing a text editor per cell.
- Gap: there is no per-target list view, so once a target is added its range and curve cannot be changed from the UI. The model supports both and the tests cover them; a small `MacroTargetsView` (or reusing the add popup on the existing target) would close it.
- Gap: a macro knob cannot be learned onto MIDI or a key from the UI yet — Learn is offered by the editor's controls and the slot switch only. The registry params exist and the router would drive them, so this is a menu item plus a callback away.
- Not persisted (same as mappings): macro names and targets are memory-only. Phase 4's `BoardSerializer` already has a place for them (`MacroData`, targets by slot position).
- Note for the render tool: `--render-slots` and `--render-editor` now draw the real macro strip, so that lie is gone. The painted editor placeholder in `--render-slots` still invents its own knob names; only `--render-editor` shows the real panel.
- Not verified: every click. The macro menus, the rename and add-target dialogs, the pick-a-knob flow, and a knob actually reaching a pedal's control are unexercised. The render proves the painting and layout, not the interaction, and nobody has judged how a macro feels while playing.

## 2026-11-07 — card 4.1 (BoardSerializer)
- What: `Source/Presets/BoardSerializer.cpp` implemented — `toJson` builds a `juce::DynamicObject` tree with the schema's own keys, `fromJson` parses one back into `BoardData`. `Tests/BoardSerializerTests.cpp` (new): six tests — round trip, every optional field missing, garbage and newer files, over-long lists, unreadable pieces, and a readable file.
- Verified: build clean, 0 warnings in our code, **1192 expectations pass** (up from 1069). Proved: data → json → data → json gives the same string; `{"schemaVersion":1}` alone is a valid empty board; `on` defaults to true and `params` to nothing; 20 slots become 16 with a warning, 12 macros become 8; `presetNext`, `programChange` and the four curves all come back in the schema's own spelling; garbage, a missing version and schema 2 are refused with `ok = false` and leave the caller's data untouched.
- Decisions: a missing `schemaVersion` is an error (the schema requires it) and a newer version is refused with a message naming both numbers; everything else missing gets its default, including `slots` (an empty board) and `name` (`""`). `toJson` always writes both trims and all three lists, so a file never depends on a default it does not state.
- Deviation: six decimal places, not the double's full tail. The model's values are floats, so a value needing more than about seven significant digits cannot round-trip exactly anyway; everything this app stores (0..1 knobs, ms, dB, BPM) sits well inside that, and the file stays readable.
- Deviation: `fromJson` is lenient about values and strict about structure — an unknown curve becomes `linear`, a mapping whose source or action is not one of the seven is dropped, an out-of-range macro value is clamped, a target slot outside 0..15 is dropped, each with a warning; but anything that is not a JSON object, or has no usable `schemaVersion`, fails the whole load.
- Bug the tests caught (worth remembering): the reader lower-cased the action name and then compared it against the camelCase spelling, so `presetNext` was silently dropped. The fix is `canonicalSource`/`canonicalAction`/`canonicalCurve`, which compare case-insensitively and return the schema's spelling — which is also why a hand-written file may say `presetnext`.
- Not verified: no file has been written or read yet. Card 4.3 is what touches the disk.

## 2026-11-07 — card 4.2 (the board ↔ BoardData bridge)
- What: `Source/Presets/BoardState.h/.cpp` (new) — `capture (model, macros, learn, registry)` reads the slots, all eight macros and the mapping list into `BoardData`; `apply (data, ...)` clears the model and rebuilds all three. `LearnController::setMappings` added (our own class, not a scaffold header) so a load publishes one mapping table instead of one per row. `Tests/BoardStateTests.cpp` (new): seven tests.
- Verified: build clean, 0 warnings, **1269 expectations pass** (up from 1192). Proved: capture → apply → capture is identical, compared through `toJson` so positions and order are included; capture → file → parse → apply rebuilds the same board; an unknown effect id leaves an empty position, the pedal after it keeps its mapping, and a re-save names the position the pedal actually has; a mapping with an unknown action, a mapping onto a missing pedal and both kinds of dead macro target are dropped with warnings; preset mappings carry no target and come back with `paramIndex == kInvalid`; loading an empty board clears slots, macros and mappings instead of inheriting the previous board's; the trims round-trip through the registry.
- Decisions: slot references are positions (`slot/<position>/<paramId>`), converted from uids on capture and back on apply — uids are handed out per launch, so a file that stored them would lose every mapping on the next start. A file position with no pedal gets an empty entry in the uid table, which is what keeps lookups right when a slot is dropped. `capture` writes all eight macros, which is what makes "load an empty board" clear the old macros. A macro target may only name a slot param; `MacroTargetData` has no way to say `board/bypass` or another macro, so those targets are skipped.
- Deviation: `apply` publishes after each `insert`, not once. The card asks for one snapshot, but `BoardModel::publish()` is private and the model has no batch call, and `BoardModel.h` is a fixed scaffold header. So the audio thread sees the board fill in slot by slot. In practice the loader sits behind the preset switch's ramp (card 4.3), which is where that belongs; if it is ever audible, the fix is a `replaceAll` on the model, and that needs a header decision.
- Gap for the owner: `BoardData` and `schemas/board.schema.json` have **no tempo field**, while today's `board.xml` saves the tempo. A synced delay loaded from a `.board` file would come back at whatever the board's tempo is. Either the tempo is device-wide (then the schema is right and `board.xml` was the odd one) or schema version 2 needs a `tempo`. Not changed here: `BoardSerializer.h` is a fixed header and the schema is the contract.
- Gap: `pedalFile` and `typeId == "pedal.custom"` travel through the format but nothing creates such a pedal yet (phase 5), so a custom slot loads as an unknown effect and leaves an empty position with a warning.
- Not verified: no file on disk and no wiring into the window. The app still saves and loads its old `board.xml`, which knows nothing about macros or mappings, so **a relaunch still forgets every mapping and macro** until card 4.3.
- Not verified: no launch was needed for these two cards — they add no runtime path — so the app was not restarted for them.

## 2026-11-07 — card 4.3 (presets on disk)
- What: `Source/Presets/PresetManager.h/.cpp` (new) — boards are `~/Music/pump BOARD/Boards/*.board`, listed sorted by name (that order is the program-change number), with `read` / `write` / `saveAs` / `fileFor` / `nameOf`. `Source/UI/WarningBar.h/.cpp` (new) — a dismissable bar for what a load had to drop. `BoardPrototype` gained `setPresetName (name, dirty)` and draws the real name plus a star in the header where the mockup's "CLEAN TO CHAOS" used to be. `MainContent` now saves and loads *whole boards*: a working copy at `~/Music/pump BOARD/current.board` is written 800 ms after the board stops changing and on quit, a launch reopens it with slots, macros and mappings, SAVE writes the named preset, Save As names a new one, `<` / `>` / program changes step the sorted list, and a swap goes bypass → 25 ms → apply → unbypass. `Tests/PresetManagerTests.cpp` (new): five tests.
- Verified: build clean, 0 warnings in our code, **1312 expectations pass** (up from 1269). Proved at the core: the folder is the list and sorts by name whatever order the saves arrived in; a file keeps its own name (the data's `name` is replaced by the file's); a broken or missing file returns `false` with a reason; a typed name cannot leave the folder; a name of only spaces is refused; the working copy is beside the folder, not in it. And the whole trip, which is what the card is for: a live board with a macro (logarithmic curve) and a mapping onto the slot switch → `capture` → `saveAs` → `read` → `apply` brings the pedal back with its macro target, its curve and its mapping, and `toJson` before and after is identical.
- Verified end to end by launching the app twice: the first launch logged "imported 3 pedal(s) from the old board.xml" and wrote `current.board` (2.4 KB of the schema's JSON, slots with their values); the second launch logged "restored 3 pedal(s), 0 mapping(s)", which is the relaunch path working.
- Decisions: (a) the working copy sits *beside* `Boards/`, so it never takes a program number; (b) the file name is the preset's name and `BoardData::name` is stamped from it on write, so name → file → name round-trips; (c) the working copy remembers that name, so SAVE after a relaunch still overwrites its preset instead of asking for a new name; (d) `write` uses `replaceWithText`, which writes a temporary and moves it into place, so a crash mid-save cannot leave half a board; (e) `board.xml` is still *read* once for migration and never written again.
- Deviation: the card asks for a "PresetBar" with the name, ◀ ▶, Save, Save As and a list. No new bar widget was built: the header already has ◀ ▶ and SAVE as painted keys with real handlers, now shows the name and the unsaved star, and the PRESETS button opens the list plus "SAVE THIS BOARD AS...". That is the card's content in the chrome the design already had; a second bar would have duplicated it.
- Deviation: the click-free swap uses the board bypass as its ramp instead of a dedicated smoother. `BoardProcessor`'s bypass already crossfades over 10 ms and its header is a fixed scaffold header, so a second smoother would mean a header decision. The swap is bypass → wait 25 ms → apply → unbypass, and a board the user had bypassed is left bypassed.
- Behaviour worth knowing: while a swap is pending (25 ms) a second preset request is dropped — `pendingSwap` holds one. A human cannot press two footswitches that fast; a burst of program changes keeps only the last.
- Dead weight: `PresetLibrary` and the `presets/*.xml` path are no longer used by the window (their tests still pass, and `board.xml` is still read for the one-time import). Delete one side or the other before this gets confusing.
- Not verified: every click. The PRESETS menu, the Save As dialog, SAVE, `<` / `>`, the warning bar's DISMISS and the star's timing were not exercised — the app was launched twice and drove itself. Nobody has judged whether the bypass fade is actually inaudible, and nothing has been loaded *while* playing.
- Not verified: a preset saved by hand and reopened after an edit (SAVE overwriting the right file) — the pieces are tested, the button is not.

## 2026-11-07 — card 0.4 (round-trip measurement)
- What: `Source/Engine/LatencyProbe.h/.cpp` (new) — `prepare()` allocates a half-second record buffer on the message thread, `begin()` arms the probe, the audio-side `process()` adds one four-sample click to the outputs on the armed block and copies input 1 into the buffer, and `finish()` finds the click by peak and turns its position into milliseconds. `SettingsView` gained a MEASURE ROUND TRIP button, a result line and a `ProbeCallback` that is on the device only while measuring. `Tests/LatencyProbeTests.cpp` (new): five tests.
- Verified: build clean, 0 warnings, **1342 expectations pass** (up from 1312). Proved: peak and first crossing where they should be; a click returning 96 samples later reads 2.00 ms at 48 kHz; one block later reads 1.33 ms; the window is ~375 blocks of 64 at 48k; the click goes out exactly once; nothing is added or recorded before `begin()`, and an unprepared probe refuses to arm; silence reads −1 (nothing came back) instead of 0 ms; a second measurement starts from a clean buffer and `cancel()` abandons one.
- Decisions: (a) the probe rides the device as a second callback instead of changing `BoardProcessor`, whose header is fixed — it sees the raw input and adds the click *after* the chain has written its block, so the click is measured unprocessed; (b) only input 1 is recorded, because a loopback is one cable; (c) the threshold is half the window's peak rather than a fixed level, so a quiet interface still finds its click; (d) "nothing came back" is −1 and is shown as CHECK THE LOOPBACK CABLE, never as 0 ms; (e) the number is sample-accurate but includes the interface's own converters, which is what a player wants to know.
- Deviation: the card puts the click behind a flag in `BoardProcessor`. It is not there: adding a measurement hook to the audio path of the shipping processor is worse than a callback that exists only during a measurement. The cost is that the click bypasses the input trim, the chain and the limiter — correct for a probe, wrong for anything else.
- Deviation: the 1 ms accuracy across three runs that the card asks for is not asserted anywhere; it needs hardware. The suite proves the arithmetic, not the interface.
- Not verified: the measurement itself. Without a cable from an output to an input there is nothing to measure; nobody has compared the measured number with the reported one, and the Settings button, its tooltip and the result line have not been clicked or seen.

## 2026-11-07 — card 5.1 (pump CRUSH adapter)
- What: `Source/Effects/adapters/PumpCrushAdapter.h/.cpp` (new) — the fourth pump engine behind `IEffect`, reproducing pump's `CrushParams.hpp` layout symbol for symbol: 16 params (voicing, drive, bits, rate, jitter, media order, wow, flutter, hiss, crackle, dropouts, age, low cut, high cut, mix, output gain), no `bypass` because the slot's switch is the board's bypass. `Tests/CrushNullTests.cpp` (new): layout, latency, mono-block behaviour and three settings × three block sizes. Registered in `BuiltInEffects.cpp` under `drive`.
- Verified: build clean, **0 warnings**, **1366 expectations pass** (up from 1342). The null test is exact, not close: peak difference **0.0000000000** (−200 dBFS, the bottom of the dB scale) at 64, 128 and 511 samples for default settings, for a worn-cassette setting (wow, flutter, hiss, dropouts, age) and for media-before-sampler with toy voicing, 6-bit, 9 kHz, crackle and parallel mix. That includes the seeded noise stages, so both sides are drawing the same random sequence. Latency is reported as pump's: 288 samples (6.00 ms) at 48 kHz, 0 before `prepare`.
- Deviation: CRUSH's media stage runs a fixed 6 ms transport and its *dry* path is delayed by the same amount, so the whole effect is 6 ms late. The adapter reports it through `latencySamples()`, but **nothing in the chain consumes that number** (`grep latencySamples Source/` finds only `IEffect.h` and the adapters). TUBE has had the same problem since card 1.4 — its oversampler latency is reported and ignored. So a CRUSH or TUBE slot delays every slot after it by 6 ms / its oversampler, and the board does not compensate. This is a chain design decision that belongs to card 6.2 (`PedalGraph.cpp` is still a TODO), not to this card; it is logged rather than half-fixed here.
- Deviation: pump marks `order` (media after/before) as an integer switch; the scaffold's `ParamInfo` only distinguishes 0/1 toggles, and calling a two-position routing switch "on/off" would be worse than showing it as a knob, so it is numeric like `mode`. Same open question as the map's item 1 (no choice labels), unchanged.
- Not verified: how it sounds; whether the 6 ms is audible as a delay when a CRUSH sits mid-board; the pedal picker showing a fourth pedal (the registry change is tested only indirectly — the null test constructs the adapter directly).
- Also fixed here: a real `-Wfloat-equal` warning in `BoardProcessor.cpp` (the tempo change test compared two floats with `!=`). It is now a difference test with a 0.001 BPM threshold, which is also what the code meant: a tempo that moved by less than a hundredth is not a change.

## 2026-11-07 — card 5.2 (pump SWIRL adapter)
- What: `Source/Effects/adapters/PumpSwirlAdapter.h/.cpp` (new) — pump's chorus/flanger/phaser behind `IEffect`, 13 params (type, variant, rate, sync, division, depth, manual, feedback, spread, tone, warmth, mix, output gain) matching `SwirlParams.hpp` symbol for symbol. `Tests/SwirlNullTests.cpp` (new). Registered under `modulation`.
- Verified: build clean, **0 warnings**, **1397 expectations pass** (up from 1366). Null difference is **0.0000000000** at 64, 128 and 511 samples for four settings: the default analog chorus, an ensemble (bucket-brigade hiss from a seeded RNG, cross-mix, high cut, feedback), the through-zero flanger (which carries 3 ms of latency on both sides) and a tempo-synced 1/8 phaser. Latency is 0 for every type except THRU-0, where it reads exactly 144 samples (3.00 ms) at 48 kHz, before and after `prepare`.
- Decision: SWIRL takes its tempo as a `pump::Transport`, not a bare BPM, and the board has no host transport. The adapter keeps one Transport, sets `bpm` from `setTempo`, and leaves `playing`/`valid` false, so a synced LFO free-runs at the right rate instead of pretending to follow a song position. Both sides of the null test are built in the same order (**settings and transport, then prepare**) because pump's `prepare` reads the settings to choose the starting type: build the reference the other way round and it starts with a pending structural switch, fading its first 6 ms, while the adapter does not. That is why the null is exact rather than "close".
- Deviation: `variant` (1..4) and `type` are numeric params with no choice labels, like CRUSH's voicing — the scaffold's `ParamInfo` has no choices field (the map's open question 1).
- Not verified: how it sounds; whether the ensemble's bucket-brigade hiss reads as musical at Warmth 100; and the pedal picker showing the fifth pedal (the registry entry is only covered indirectly).
- Gap now three deep: TUBE's oversampler latency, CRUSH's 6 ms transport and SWIRL's 3 ms THRU-0 are all reported by `latencySamples()` and all ignored by the chain. Card 6.2 has to settle it; until then a slot of any of these three delays its downstream neighbours.

## 2026-11-07 — card 5.3 (pump COMP adapter)
- What: `Source/Effects/adapters/PumpCompAdapter.h/.cpp` (new) — pump's six-design mastering compressor behind `IEffect`: 22 params (style, threshold, ratio, knee, range, attack, release, auto release, hold, lookahead, mix, input gain, output gain, auto makeup, stereo link, channel mode, sidechain source/gain/high-pass/low-pass/listen, oversampling) matching `CompParams.hpp`. `Tests/CompNullTests.cpp` (new). Registered under `dynamics`.
- Verified: build clean, **0 warnings**, **1425 expectations pass** (up from 1397). Null difference **0.0000000000** at 64, 128 and 511 samples for four settings: pump's defaults, master glue (auto release + auto makeup), punch (feedback topology) with 2 ms lookahead **and 2x oversampling**, and vari-mu (tube colour) in mid/side at 60% link. Latency follows the params: 0 at pump's defaults, exactly 96 samples (2.00 ms) with 2 ms lookahead, and it matches pump's own `latency()`.
- Decision: the adapter passes null sidechain pointers, because a slot owns no sidechain bus. pump's engine treats null as silence, so a slot with Sidechain Source = External produces no gain reduction rather than ducking to nothing — the safe failure. The knob is still exposed so the layout matches pump and the parameter exists for a later sidechain routing card.
- Decision: `process` is handed the buffer as both input and output, which pump's engine documents as in-place safe; the null test drives the reference with separate buffers and still lands on 0.0, so the two paths agree.
- Deviation: `style` (six names) and `channel_mode` (four names) are numeric params with no choice labels — the scaffold's `ParamInfo` has no choices field (the map's open question 1). `ratio` is linear on the knob; pump shows it on a power scale, so the midpoint reads differently from pump's UI even though the value handed to the engine is the same.
- Not verified: how it sounds; whether the Vari-Mu colour is audible as character at 24 dB of reduction; whether 2 ms of lookahead at 64 samples is enough to prevent overshoot on a guitar signal.
- Gap now four deep: TUBE's oversampler, CRUSH's 6 ms transport, SWIRL's THRU-0 3 ms and COMP's lookahead/oversampling are all reported by `latencySamples()` and all ignored by the chain. COMP makes this urgent rather than merely wrong: lookahead and oversampling are ordinary knobs a player will touch, and every turn of them re-times the rest of the board.

## 2026-11-07 — card 5.4 (latency alignment) and card 5.5 (the light skin)
Asked for by the owner together, to test by hand.

### 5.4 — the dry paths now run at the chain's speed
- What: `Slot` and `BoardProcessor` turned their dry copies into **delay rings**. A slot reads its dry copy back one effect-latency behind the input, and the board reads its own back one chain-latency behind (the sum of the slots' reported latencies, recomputed each block so a lookahead or oversampling knob re-times it immediately). The rings are 8192 frames per slot (170 ms) and 16384 for the board (340 ms), allocated in `prepare`, never resized on the audio thread. `Tests/SlotLatencyTests.cpp` (new, 3 tests).
- Why: the crossfades were comparing two different moments. Switching off a CRUSH slot crossfaded a 6 ms-late wet signal with an early dry one — a comb filter, not a bypass — and the board bypass did the same with the whole chain's latency.
- **Scaffold header change, raised here rather than made quietly:** `Slot.h` and `BoardProcessor.h` each gained one private `int dryWritePos`. A delay ring cannot exist without a position to read from, and the only alternatives were worse: duplicating the crossfade in a new class (two implementations of bypass, the thing the handover already warns about) or allocating a ring per block (allocation on the audio thread). Both headers are marked "fixed interface — do not change without review"; this is that review, and the change is one int in each.
- Verified: build clean, **0 warnings**, **1431 expectations pass** (up from 1425). The new tests prove: a slot with a latency-reporting effect passes an impulse untouched while on; switching it on from bypass returns the impulse exactly four samples late when the effect reports four samples; a zero-latency effect still returns it in the same block. The existing `SlotTests` and `BoardProcessorTests` caught the first version of this — I had forgotten to subtract the block length when working out the read position, so the dry copy was read from beyond what had just been written; pass-through tests failed immediately and the arithmetic was fixed rather than the tests.
- Not verified: the compensation by ear. Nothing has been recorded through a CRUSH or COMP slot while switching it, and the board bypass fade has not been heard with latency on the board. The rings are also untested at their ends (a chain deeper than 340 ms would be clamped, silently).
- Still true at the time: the chain's *total* latency was not reported anywhere. The status line showed the device's round trip only, so a board full of COMP was later than it looked. (Card 5.6, below, fixed that.)

### 5.5 — the light skin
- What: `style::darkMode()` is one process-wide flag; every panel that builds a `Theme` now reads it (`style::isDark()`), including the painted board. Settings gained a **SKIN: DARK / SKIN: LIGHT** button. Because each panel builds its palette in its constructor, flipping the flag is not enough: the button asks the window to rebuild its content on the next message (`MessageManager::callAsync`), so the rebuild can never delete the button mid-click. `MainWindow` kept its engine references for exactly this and rebuilds the content in place, at the same window bounds.
- Verified: build clean, 0 warnings, all 1431 expectations pass (the skin is not covered by a unit test — it is layout and colour).
- Decisions: the choice is **per session**, not saved; it is not in `audio.xml` because that file is the device setup, not a UI preference. The rebuild round-trips through the working board (`~MainContent` saves, the new one loads), which is the existing, tested path.
- Not verified: the light palette has not been looked at. It is pump's own `Theme::build(a, false)` maths, unchanged and already used by `--render-editor`'s light render, but no one has seen the full window in it. The window's own background colour is still the dark `0xff141418` from the constructor, so a light skin may show a dark rim where the content does not reach.

## 2026-11-07 — card 5.6 (the board's latency in the status line)
- What: the status line in `MainContent` now sums `latencySamples()` over the slots the model holds and, when the sum is not zero, appends `BOARD <ms>` after the device's round trip. Nothing else changed: no new accessor, no header change. The sum is taken on the message thread at the existing 30 Hz status tick, so it follows a lookahead or oversampling knob without touching the audio path.
- Why: card 5.4 made the dry paths honest but left the chain's own latency uncounted. A board with COMP on it is later than the round trip beside it suggests, and the number that says so is the one already reported by every adapter.
- Verified: build clean, **0 warnings**, **1431 expectations pass** (unchanged — this is a display line, and the app target is not linked by the tests). The app launches, publishes its slots, restores its pedals and exits without an assertion.
- Decision: the figure is shown only when it is not zero. A board with nothing latency-reporting on it keeps the old, shorter line instead of reading `BOARD 0.0 ms`. It is in milliseconds to match the round trip next to it, not in samples.
- Not verified: that the text fits. The status line is laid out by the existing resized() and has not been seen with the extra field on a narrow window; a long device error string next to it could crowd it.

## 2026-11-07 — card 5.7 (pump HEAT adapter)
- What: `Source/Effects/adapters/PumpHeatAdapter.h/.cpp` (new) — pump's 1-4 band saturation/exciter behind `IEffect`. 37 params: the nine global ones (output gain, mix, oversampling, auto gain, crossover mode, band count, three crossovers) and four bands of seven (curve, drive, mix, level, solo, mute, band bypass). pump's global `bypass` and its unused `listen_band` placeholder are not exposed. `Tests/HeatNullTests.cpp` (new). Registered under `drive`.
- **Build change:** HEAT's linear-phase splitter goes through pump's `RealFFT`, which is pffft, so `external/pump/plugins/heat/pffft.c` joined `PUMP_DSP_SOURCES`. Every pump plugin ships its own copy of that one C file; one is enough for the app, and this unblocks every FFT-based pedal left in phase 5 (TONE, CALM, ROOM, EQ, MASTER).
- Verified: build clean, **0 warnings**, **1456 expectations pass** (up from 1431). Null difference **0.0000000000** at 64, 128 and 511 samples for four settings: pump's defaults (3 bands, 4x), tube in the mids with the global mix at 60%, 8x oversampling with linear-phase crossovers and auto gain off, and per-band clip with a muted band. Latency is reported from the engine (splitter + oversampler) and matches pump's own `latency()`.
- Lesson worth keeping: the first null run failed at about -14 dBFS, and it was the *test* that was wrong. HEAT has no settings struct — the adapter prepares pump's engine and then hands it setters — and my harness called `adapter.reset()` first, which prepares the engine a second time with the tweaked values already in place, so the adapter started on the new curve while the reference crossfaded into it over 12 ms. Both are legitimate states; comparing across them is not a null test. The harness now does exactly what `prepare()` does and nothing more.
- Deviation: `oversampling` defaults to 4x (pump's own default), so a freshly placed HEAT slot reports real latency. That is honest and card 5.4 compensates it, but a player who hears a shift when adding HEAT will be hearing pump's default, not a bug.
- Not verified: how it sounds; whether the auto-gain level match feels right on guitar at 6 dB of drive; and the pedal picker showing seven pedals (the registry entry is covered indirectly).

## 2026-11-07 — the pedal menu opens over its slot (owner's report)
- What: `MainWindow::showSlotMenu` anchored the popup with `withTargetComponent (&board)` — the whole board — so JUCE put it at the board's corner no matter which slot was clicked. It now anchors to that slot's view, falling back to the board when the index has no view. A few lines in `Source/App/MainWindow.cpp` and nothing else.
- Verified: build clean, 0 warnings, **1483 expectations pass**, the app launches and exits with no assertions.
- Not verified: that it lands where the eye expects on every slot, and what it does in the right-hand column and on the bottom row, where it has to flip to stay on screen. JUCE keeps the popup inside the window; the placement has not been looked at.

## 2026-11-07 — card 5.8 (pump TONE adapter)
- What: `Source/Effects/adapters/PumpToneAdapter.h/.cpp` (new) — pump's adaptive balancing EQ behind `IEffect`, 11 params: recover, tame, bias, brighten, body, speed, auto level, output gain, mix, quality, delta. pump's `bypass` and its three `audition*` placeholders are not exposed (the same reasons as HEAT). `Tests/ToneNullTests.cpp` (new). Registered under `filter`.
- Verified: build clean, **0 warnings**, **1483 expectations pass** (up from 1456). Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a highs-taming setting, and 4096-point quality with Body up, auto level off and mix at 70%.
- Latency: the STFT size — 2048 samples (43 ms at 48k) at quality 0, 4096 (85 ms) at quality 1 — read from the engine, so card 5.4 compensates it and card 5.6 shows it. TONE is the pedal that makes those two cards matter: it has the longest latency on the board by a wide margin.
- Lesson (a): a settings change is `prepare`, then `set` — never a second `prepare` after the settings are already in. HEAT taught this and TONE confirms it; here `set` also steers the quality switch, so doing it the other way round starts the pedal on a different curve without the crossfade pump intends.
- Lesson (b): `quality` is a *request*. The engine fades out over 5 ms, switches at a chunk boundary and stays muted for the new size, so `latencySamples()` stays at 2048 until audio has run through the switch. The test asserts both halves — 2048 before, 4096 after ten blocks — and that the outputs stayed identical across the switch.
- Deviation, and a landmine to revisit: that latency change happens **mid-stream**, and card 5.4's dry ring reads the effect's latency every block, so switching quality while playing moves the dry path by 2048 samples at once. The wet side is muted around the switch, so it should read as a short dropout rather than a click, but fading the dry path across the change would be the proper answer. Not fixed, not verified by ear.
- Not verified: how it sounds; whether 43 ms is playable on guitar (my guess is that as a board effect it is for shaping tone after the fact, not for feel); and the picker showing eight pedals.

## 2026-11-07 — card 5.9 (pump VERB adapter)
- What: `Source/Effects/adapters/PumpVerbAdapter.h/.cpp` (new) — pump's algorithmic reverb behind `IEffect`: 25 params (12 modes including vintage, classic, 90s rack, gated, reverse and shimmer, plus size, decay, diffusion, early/late, bass multiplier, crossover, damping, filters, modulation, width, age, gate time, shimmer pitch/amount, duck, freeze, mix, output). pump's `bypass` is not exposed. `Tests/VerbNullTests.cpp` (new). Registered under `space`, so the board now has two reverbs with very different characters: SPRING's physical tank and VERB's halls.
- Verified: build clean, **0 warnings**, **1527 expectations pass** (up from 1483). Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a long plate with shimmer, and a gated mode with ducking and pre-delay.
- Latency: zero, as pump reports. Only the board's 128-sample buffer stands between the string and the sound, which is what makes VERB the natural first reverb to reach for.
- The pre-delay can sync to the tempo, and there is no host: the adapter holds 120 BPM and exposes `setTempo()`. pump's own default is unsynced, so nothing is lost today; the moment a real tempo arrives, that is the one call to make.
- Not verified: how it sounds; whether the shimmer and gated modes are usable on guitar; and — importantly — the null tests run for half a second, which is enough to compare the engines but says nothing about whether a 30 s decay actually decays. Long tails are untested.

## 2026-11-07 — card 5.10 (pump LIMIT adapter)
- What: `Source/Effects/adapters/PumpLimitAdapter.h/.cpp` (new) — pump's true-peak limiter, 14 params: gain, ceiling, seven styles, lookahead, attack, release, both channel links, true peak, oversampling, DC filter, dither, noise shaping and unity-gain listen. Registered under `dynamics`. `Tests/LimitNullTests.cpp` (new).
- pump's limiter has **no `Settings` struct** — it takes a setter per control — so the adapter owns a small one and converts it in exactly one place, `applySettingsTo()`, a template the null test also uses on its reference engine. That is deliberate: with twelve setters, hand-copied ordering in a test is the kind of thing that quietly stops matching.
- Not exposed: pump's `bypass` (the slot's switch), and `target` / `target_custom`, which drive pump's own loudness meter and do not change the audio.
- Verified: build clean, **0 warnings**, **1527 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a modern 4x-oversampled setting, and a safe setting with 16-bit dither and noise shaping — pump's dither is deterministic across two engines, so it stays a valid null test.
- Latency: lookahead plus oversampling, read from the engine. Both are configuration controls, and pump reconfigures on the audio thread at the next block start, so this is the third pedal whose latency can move mid-stream (with HEAT and TONE). The same landmine applies: card 5.4's dry ring follows the new value in one block.
- Two interface facts fixed by the compiler, worth remembering for the next adapter: VERB and LIMIT both declare `prepare(sampleRate, maxBlock)` while TONE declares `prepare(sampleRate)`, and the note-division constants (`kNote32`) live in `namespace pump`, not in the effect's own namespace.
- Not verified: gain staging on guitar at 9-11 dB of gain; whether a limiter belongs on a guitar board at all (it is the utility at the end of the chain, and the owner asked for more effects rather than this one specifically); and the picker showing ten pedals.

## 2026-11-07 — card 5.11 (pump SNAP adapter)
- What: `Source/Effects/adapters/PumpSnapAdapter.h/.cpp` (new) — pump's transient designer behind `IEffect`, 14 params: attack, sustain, attack time, release, stereo link, detector focus, band, crossover, lookahead, clip with ceiling, delta, mix, output. Registered under `dynamics`. `Tests/SnapNullTests.cpp` (new).
- Verified: build clean, **0 warnings**, **1596 expectations pass** (up from 1527). Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, an attack-boosting setting with 3 ms of lookahead, and a high-band setting with clipping.
- Latency: the lookahead, and it is off by pump's default, so a fresh SNAP slot adds nothing. Switching it on moves `latencySamples()` after a short fade.
- Lesson: the lookahead is a *structural* change in pump — it fades the output down, then moves the delay, putting samples into the ring rather than removing them. Reading `latency()` after a single block therefore still says 0, which is what my first test did and what failed. The test now runs both engines for sixteen blocks and compares afterwards: 192 samples on both sides for 4 ms.
- Not verified: how it sounds on a picked note; whether the clip stage is worth having after a limiter already exists in the chain; and the picker showing eleven pedals.

## 2026-11-07 — card 5.12 (pump STRIP adapter)
- What: `Source/Effects/adapters/PumpStripAdapter.h/.cpp` (new) — pump's console channel strip, 37 params: eight input models (Clean, Class A '73, Console '79, Console '87, Discrete '70, Valve, Studio, Broadcast), input, colour, phase invert, HPF and LPF, a four-band EQ with bell switches on LF and HF plus Q on both mids, a gate with mode/range/release, a compressor with ratio/auto release/makeup/mix and its own sidechain HPF, an EQ-before-or-after-dynamics switch, output, auto gain and oversampling. Registered under `drive`. `Tests/StripNullTests.cpp` (new).
- Verified: build clean, **0 warnings**, **1596 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, an EQ-and-compressor setting on a coloured model, and a phase-inverted gated setting with filters and 4x oversampling.
- Latency: the oversampler. pump's default is 2x, so a fresh STRIP slot does report latency — and, as the test found, even 1x costs 63 samples of filter delay at 48k. Do not assume 1x is free.
- Lesson: the model constants are pump's own names (`kClassA`, `kConsole79`, …) and there is no `kNeve`; the test now uses index 1 with a comment rather than inventing a name for pump's model.
- Not verified: whether the eight models are audibly different on a guitar; how the gate behaves with a real noisy pickup; and the picker showing twelve pedals.

## 2026-11-07 — card 5.13 (pump CALM adapter)
- What: `Source/Effects/adapters/PumpCalmAdapter.h/.cpp` (new) — pump's resonance and harshness catcher: it finds narrow peaks and pulls them down without touching the broad balance. 14 params: depth, sharpness, selectivity, attack, release, mode, focus low/high, stereo mode, balance, quality, delta, mix, trim. Registered under `filter`. `Tests/CalmNullTests.cpp` (new).
- Latency: the FFT size, like TONE (default quality 1), so the dry paths compensate it.
- **Deviation:** pump's own UI lets you drag the frequencies and gains of 48 individual resonance nodes. The board exposes only the global shape and uses pump's default node set, because `ParamInfo` has no way to describe 96 more controls and the slot editor is a generic knob grid. That is a real loss of control versus pump's editor, and it is written here rather than worked around.
- Verified: build clean, **0 warnings**, **1596 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a high-mid hunting setting, and a side-only setting with delta at the largest FFT.
- Not verified: how it sounds on guitar; whether the default node placement is the right one for a guitar rather than for a mix; and the picker showing thirteen pedals.

## 2026-11-07 — card 5.14 (pump GATE adapter)
- What: `Source/Effects/adapters/PumpGateAdapter.h/.cpp` (new) — pump's tempo-locked step gate: 1..32 steps, a note rate, swing, gate length, attack/release, smoothing, depth, three modes (level, filter, both), a filter with resonance, a stereo offset, mix and output. 15 params. Registered under `dynamics`. `Tests/GateNullTests.cpp` (new).
- The sequencer needs a tempo, and the board has none to give an effect yet (the header's tempo field drives ECHO's own sync, not the adapters). GATE therefore free-runs at the **adapter's own 120 BPM** through `pump::Transport`, exactly as SWIRL does, and exposes `setTempo()` for the day the board's tempo reaches the effects. Same arrangement in the two cards below.
- **Deviation:** the per-step pattern (32 levels) is a curve, not a knob, and `ParamInfo` cannot describe it, so the pedal starts on pump's default pattern and the pattern is not editable on the board. That is the single biggest loss in this adapter and it is the same class of gap as CALM's 48 nodes.
- Verified: build clean, **0 warnings**, **1663 expectations pass** (up from 1596). Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, swung eighths, and filter mode with resonance and a stereo offset.
- Not verified: whether the default pattern is musically useful; how the gate feels against a real picked guitar (attack and release are down to 0 ms); and the picker showing fourteen pedals.

## 2026-11-07 — card 5.15 (pump GRAIN adapter)
- What: `Source/Effects/adapters/PumpGrainAdapter.h/.cpp` (new) — granular: position, grain size, density, spray, tempo sync with 18 divisions, five envelopes, reverse, stereo spread, pitch and pitch spread, seven scales, feedback, four filter modes with cutoff and resonance, space, motion rate/depth, a two-axis morph and freeze. 24 params, the widest modulation pedal on the board. Registered under `space`. `Tests/GrainNullTests.cpp` (new).
- Verified: build clean, **0 warnings**, **1663 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a pitched minor scale, and a synced filtered setting.
- Latency: zero, as pump reports. Its grains read from a buffer the engine fills itself, so nothing is added to the chain.
- Not verified: how it sounds on guitar; whether 1..100 grains per second is the useful range for a plucked source; and whether the octave/fifth scales fight the input's own pitch (pump offers quantisation, not pitch tracking).

## 2026-11-07 — card 5.16 (pump STUTTER adapter)
- What: `Source/Effects/adapters/PumpStutterAdapter.h/.cpp` (new) — tempo-locked stutters and tape stops: auto or manual trigger, interval, chance, length, grid, gate, pitch/decay/filter/variation per repeat, tape stop time, 12 factory patterns plus a user pattern. 16 params. Registered under `time`. `Tests/StutterNullTests.cpp` (new).
- **Deviation:** pump's eight move toggles (`use[kNumMoves]`) are not parameters in pump either — its UI sets them from keys — so the pedal runs with pump's defaults (the first four enabled). Same reason as GATE's pattern: `ParamInfo` has no way to express them.
- Verified: build clean, **0 warnings**, **1663 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a pitched and decaying setting, and a manual setting with a pattern.
- Not verified: how it sounds; whether the auto trigger's 35% chance is musical rather than arbitrary; and whether a stutter offers anything to a guitarist that GRAIN does not.

## 2026-11-07 — the tempo question, now that three pedals need one
- GATE, GRAIN, STUTTER and SWIRL all take a `pump::Transport`. Each adapter holds its own 120 BPM and offers `setTempo()`; nothing on the board calls it. The header's tempo field exists and ECHO uses it for its own sync, but the effect path has no tempo hook at all: `IEffect` has no `setTempo`, and `Slot`/`BoardProcessor` do not carry one.
- That is the next structural card, not a bug in any adapter: a tempo change should reach every slot at once, at block start, without touching the audio thread's allocations. Written down here so the next session does not rediscover it from four `setTempo()` methods that nobody calls (five, after CHOP).

## 2026-11-07 — card 5.17 (pump CHOP adapter)
- What: `Source/Effects/adapters/PumpChopAdapter.h/.cpp` (new) — pump's performance chopper: eight pads, a length per pad (half bar to four bars), quantised starts, trigger mode, retrigger, reverse, gate/pitch/decay per hit, mix and output, recording the last bar so a pad can re-cut what was just played. 16 params. Registered under `time`. `Tests/ChopNullTests.cpp` (new).
- Tempo: like GATE, GRAIN and STUTTER it holds its own 120 BPM `pump::Transport` and exposes `setTempo()`; nothing on the board calls it yet.
- Unit trap: `pump::chop::Settings::mix` is 0..1 while the parameter is 0..100. CHOP is the only pedal here where the parameter's unit and the engine's differ, so `setParam` divides by 100.
- Verified: build clean, **0 warnings**, **1684 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a single one-bar pad, and a reversed, pitched setting.
- Not verified: how it sounds against a real take; whether 8 pads is enough to play it rather than program it.

## 2026-11-07 — card 5.18 (pump WIDE adapter)
- What: `Source/Effects/adapters/PumpWideAdapter.h/.cpp` (new) — multiband stereo widener: 1..4 bands with three crossovers, per-band width and stereoize, mono bass with an optional steeper slope, balance and output. 18 params: pump's ten globals (minus `bypass` and the hidden `listen_band`) plus four bands of width/stereoize. Registered under `space`. `Tests/WideNullTests.cpp` (new).
- Verified: build clean, **0 warnings**, **1703 expectations pass**. Null difference **0.0000000000** at 64 and 511 samples for all three settings, and **0.0000000025** (about −172 dBFS) at block 128 for the "two bands, balanced" setting. That is inside the card's −90 dBFS bar but not bit-exact, and it is written here as measured rather than rounded down to zero — the likely cause is the splitter settling when the band count changes, which is not the same thing as an uninitialised state.
- Not verified: whether widening does anything a guitarist wants on a signal that is usually close to mono; how the linear-phase crossover mode sounds.

## 2026-11-07 — card 5.19 (pump BAND adapter)
- What: `Source/Effects/adapters/PumpBandAdapter.h/.cpp` (new) — multiband compressor: 1..4 bands with three crossovers, peak or RMS detection, stereo link, lookahead, auto makeup, mix and output, plus per-band threshold, ratio, attack, release and makeup. 31 params. Registered under `dynamics`. `Tests/BandNullTests.cpp` (new).
- **Deviation:** pump's own parameter table has no per-band controls at all (thirteen globals; its editor writes `BandSettings` directly). The twenty per-band parameters are therefore this adapter's own layout, in pump's units. Band mode, knee, range, solo, mute and band bypass stay on pump's defaults.
- Verified: build clean, **0 warnings**, **1723 expectations pass** (with BAND as the newest pedal). Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, three compressed bands, and RMS detection with lookahead, auto makeup and four bands.
- Not verified: how much a guitarist needs a four-band compressor band; whether pump's 12 ms band-mode crossfade is audible when a band's controls are changed while playing.

## 2026-11-07 — card 5.20 (pump EQ adapter)
- What: `Source/Effects/adapters/PumpEqAdapter.h/.cpp` (new) — 24-band dynamic EQ: output, gain scale, auto gain, phase mode, linear-phase quality, dynamic attack/release, and per band used/shape/frequency/gain/Q/dynamic. 55 params: seven globals plus eight bands of six. Registered under `filter`. `Tests/EqNullTests.cpp` (new).
- **Deviation:** pump offers 24 bands; the board exposes the first **eight** and leaves 9..24 unused, which is how pump ships them anyway. Per-band slope (12 dB/oct), stereo placement and the dynamic range/threshold stay on pump's defaults. Same class of loss as CALM's 48 nodes and GATE's pattern, and the largest one left.
- Verified: build clean, **0 warnings**, **1745 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults (no bands on), shelves and bells with a gain scale, and a dynamic cut beside a narrow notch.
- Not verified: **the linear-phase mode.** Its kernels are built on a worker thread, so two engines can be a frame apart while they settle; the null test therefore compares only pump's zero-latency mode. Also not verified: how eight bands feel against pump's twenty-four, and whether auto gain is useful on a guitar.
- Lesson (mine, not pump's): the first version of the EQ test asserted that `params[10]` was band 1's Q. It is its gain — the stride is used, shape, frequency, gain, Q, dynamic. The adapter was right and the test was wrong, so the index was fixed, not the code. Worth remembering: a failing expectation is not automatically an adapter bug.

## 2026-11-07 — phase 5 closes: what is in, what is out, and why
- **In:** twenty pedals — SPRING, ECHO, TUBE, CRUSH, SWIRL, COMP, HEAT, TONE, VERB, LIMIT, SNAP, STRIP, CALM, GATE, GRAIN, STUTTER, CHOP, WIDE, BAND, EQ. Every one null-tested against pump's own engine.
- **Out at the owner's request:** METER (it is a metering engine — read-only output parameters, no audio change, so a slot for it would be a slot that does nothing) and MASTER (an adapter was written, tested and then withdrawn; the board has its own safety limiter and output trim).
- **Out for a structural reason, needing a decision:** VOX and TUNE want a carrier or MIDI note input, and `IEffect` has no such hook — that is a scaffold change needing review, not an adapter. TWANG is built on COIL, which is an instrument engine; it is the same case as WOB.
- **Still open from earlier:** the tempo hook. Five pedals carry a `setTempo()` that nothing calls.

## 2026-11-07 — card 5.21 (pump TWANG adapter, and why it is not an instrument)
- What: `Source/Effects/adapters/PumpTwangAdapter.h/.cpp` (new) — TWANG's spring resonator: model, tension, drip, decay, release, harmonic, spread, wobble, tone, grit, tuning, root, chord, octave, drive, tank, width, mix, output. 19 params. Registered under `space`. `Tests/TwangNullTests.cpp` (new). It is the first pedal whose engine is another plugin's: `pump::coil::Engine` in `resonator` mode, compiled from `plugins/coil/`, which CMake now includes alongside the just-built pedal list.
- **The finding that unblocked it:** the earlier note said TWANG needed MIDI notes, like WOB and VOX. It does not. In resonator mode COIL excites its springs from the audio it is given (`bodyIn = dry * 0.7 + in * x * 2.0` in `processChunk`) and mixes back through `mix`, and chord tuning ignores note events outright (`if (set.resonator && set.tuning == kTuneChord && (st == 0x90 || st == 0x80)) return;`). So a guitar drives it with no MIDI at all, which is what a slot can offer. `kick()` (TWANG's own KICK key) is deliberately not exposed — a slot has no key.
- Verified: build clean, **0 warnings**, **1765 expectations pass**. Null difference **0.0000000000** at 64, 128 and 511 samples for pump's defaults, a long bright maj7 chord with tank and width, and a gritty single string at another model. Both engines are given the same audio and the same settings through one shared `applySettingsTo()`.
- Two mistakes of mine, both in the test: a helper wrote channel 1 into a mono buffer (JUCE asserts at `AudioSampleBuffer.h:688`), and a leftover three-argument call to the same helper. Adapter code was never at fault.
- Not verified: how it sounds on a guitar rather than on the five-tone test signal; whether the default fifth chord is the right place to land for a pedalboard; and whether a resonator earns a slot next to SPRING.

## 2026-11-07 — card 5.22 (the tempo hook)
- What: the board's tempo now reaches every pedal that follows it. `BoardProcessor` already had the route — `board/tempo` (20..300 BPM, default 120) is pushed to whichever slot effects implement `ITempoAware`, on change only, before the block runs. SWIRL and ECHO had opted in; **GATE, GRAIN, STUTTER, CHOP and VERB now do too**, which is the whole change to those headers: inherit `ITempoAware`, mark the existing `setTempo` `override`.
- **Deviation, and the reason for it:** CHOP shipped with its own `bpm` parameter (pump has one). `Slot::process` pushes every parameter every block, so that knob overwrote the board's tempo immediately after it arrived — the test caught CHOP stuck at 120 while GATE followed. CHOP's BPM knob is therefore **gone** (params 8 → 7): with one tempo on a board, a second one per pedal is a knob that fights the header. Its clamp (40..240) still applies, so a board tempo of 300 reaches CHOP as 240.
- Verified: build clean, **0 warnings**, **1914 expectations pass**. `Tests/TempoReachTests.cpp` (new) proves the cast for all six pedals, that `setTempo` lands in each transport with pump's clamps, and end-to-end through `BoardProcessor` that 132 and then 300 reach a GATE slot and a CHOP slot (240 for CHOP). A SPRING slot alongside them still processes a block untouched.
- Not verified: how any of it sounds — a gate at 132 versus 120 is exactly the kind of thing that has to be heard; and whether MIDI clock or tap tempo should drive `board/tempo` (still open, still small).

## 2026-11-07 — card 5.23 (choice labels)
- What: `ParamInfo` gained an optional `choices` list, so pump's named modes print as names instead of numbers. 66 parameters across the twenty pedals now carry labels, taken from pump's own arrays (`Source/Effects/adapters/PumpChoices.h` turns a pump name array into the list, so nothing is hand-copied). The generic editor's readout uses the label; the knob still steps in whole numbers and typing a value still takes a number.
- **Scaffold change, at the owner's request** — this is the one header change of the session and it is additive: an eighth member with a default, `choiceCount()`, `choiceLabel()`, and a constructor so the ~300 existing `{ id, name, min, max, default, unit, toggle, skew }` rows keep compiling. Without the constructor the build emitted **303** `-Wmissing-field-initializers` warnings, which is how the need for it was noticed.
- Verified: build clean, **0 warnings**, **2496 expectations pass**. `Tests/ParamChoicesTests.cpp` (new) covers the lookup rules (snapping, and an out-of-range value returning nothing rather than a wrong name), a parameter whose values start at 1 (TUBE's stages), spot checks that pump's names actually arrive (SPRING "Short tank", TUBE "Triode"/"4x", TWANG "Fifth", EQ band 2 "High Cut"), and an invariant over all twenty pedals: one name per value, no empty name, and the default naming a real choice.
- Not verified: **how the labels look in the app.** The editor is UI and the test target does not link it, so this was checked by reading code, not by looking at a render.
- Left as numbers on purpose: parameters pump itself does not name (most percentages), and TWANG's octave, which has no name array.

## 2026-11-07 — card 5.24 (per-slot CPU meter)
- What: each slot's effect time is measured around `effect->process` and smoothed 90/10 in `Source/Engine/LoadMeter.h/.cpp`; `SlotView` paints a 3 px bar above the footswitch (red past 90% of the block), and the status line adds `LOAD nn%`, the sum over the live slots.
- **Work-around, not a header change:** `Slot.h` is a fixed scaffold header, so the timing state lives in a fixed 64-entry lock-free table keyed by the slot's address — registered in the constructor, unregistered in the destructor, exactly the pattern `MidiRouter` uses for its retire list. Registration is message-thread; publishing is audio-thread (two clock reads, no allocation, no locks); a read is a linear scan of at most 64 atomics.
- **A bug its own test caught:** `find(nullptr)` matched the first *free* table entry, so publishing with no slot at all wrote into an empty row and `slotLoad(nullptr)` read it back — the first test run failed with 0.1 where it expected 0. Guarded, and the test now publishes to `nullptr` and expects zero.
- Verified: build clean, **0 warnings**, **2496 expectations pass**. `Tests/LoadMeterTests.cpp` (new) covers the budget arithmetic (2.5 ms of a 10 ms block reads 0.025, then converges to 0.25 over repeated blocks), clamping for a ten-times-overrun block, `boardLoad()` equalling the sum, a destroyed slot dropping out, and a real SPRING slot feeding the meter (0.11 of the budget at 64 samples, in Debug).
- Not verified: **the bar and the `LOAD` text were never looked at.** The app was launched after the change (alive after 8 s, 0 assertions, restored its board) but no render was taken, so this is "wired and compiled", not "seen working". The figure is also Debug-build cost; a Release build will read lower, so it is a guide, not a promise.

## 2026-11-07 — card 5.25 (the tempo is the device's)
- What: `Source/Common/AppSettings.h/.cpp` (new). The tempo now lives in `~/Music/pump BOARD/settings` (a `juce::PropertiesFile`), next to `current.board`: it is read on the first status tick and written back whenever the header's tempo moves. `BoardData`/`BoardSerializer.h` are untouched — `.board` still has no tempo field, which is now the *right* shape rather than a gap.
- Why: the owner's answer to "is the tempo device-wide?" is yes. One tempo for the instrument, not a tempo trapped inside a file. It survives a board swap, a browse through presets and a relaunch.
- Decisions: (a) the value is clamped 20–300 on the way in *and* on the way out, so a hand-edited settings file cannot claim 0 BPM; (b) a missing file is the default 120, not a failure; (c) the write happens on the existing 30 Hz status tick when the value differs by more than 0.01, not on every drag frame, so dragging the tempo does not write the file thirty times a second.
- Deviation: `BoardPreset` (the one-time `board.xml` import/export) still writes and reads a tempo. That path is migration only and the JSON `apply` never touches `board/tempo`, so it was left alone.
- Verified: build clean, **0 warnings**, **2496 expectations pass**. `Tests/AppSettingsTests.cpp` (new): a missing file reads the fallback, a write reads back (and a second write replaces), and an out-of-range file is clamped.
- Not verified: **that the running app really reopens at the saved BPM.** The wiring was read and the app was launched, but the turn-knob → quit → reopen round trip was not performed by hand.

## 2026-11-07 — card 5.26 (rename and delete a board)
- What: `PresetManager::rename` / `PresetManager::remove`, and the PRESETS menu now opens a submenu per board — LOAD, RENAME..., DELETE... — instead of one flat list.
- How: rename reads the board, saves it under the new legal name (which stamps the new name *inside* the JSON via the existing `saveAs`), then trashes the old file; delete trashes the file. Deleting the board you are on clears `currentPreset`, so the next SAVE asks for a name rather than writing to a file that no longer exists.
- Decisions: (a) `moveToTrash`, not `deleteFile` — a mis-click is recoverable in the Bin; (b) rename refuses a name that already exists rather than overwriting somebody's board; (c) an empty or whitespace-only name is refused; (d) the board on screen follows its file to the new name, so SAVE keeps overwriting it instead of making a copy.
- Verified: build clean, **0 warnings**, **2496 expectations pass**. `Tests/PresetManageTests.cpp` (new): rename moves the file and the name inside it, a taken name is refused with both files intact, empty/missing are refused, and delete removes exactly one board and returns false the second time.
- Not verified: **the menu and the two dialogs were never opened.** They are UI; the tests cover the file operations they call.

## 2026-11-07 — card 5.27 (the fonts are in the app)
- What: pump's three faces are now compiled in. `juce_add_binary_data(PumpBoardFonts ...)` produces `pumpfonts::*`, `fonts::load` uses those bytes first, and only falls back to `external/pump/common/fonts` on disk if the target is absent (so the render tools still work from a source tree). The two `OFL-*.txt` licences are copied into the bundle's `Resources`, verified present in the built app.
- Why: the app used to find its fonts by walking up from the working directory to `external/pump`, which works in this repo and nowhere else. A copied `.app` would have fallen back to the system monospaced font and looked wrong.
- Deviation: pump keeps its own `FontData.cpp`; this uses JUCE's generator, so the symbols are `pumpfonts::ShareTechMonoRegular_ttf` (no underscore before the weight). The first build guessed `ShareTechMono_Regular_ttf` and failed to compile — cheap, since it only failed on the three lookups.
- Verified: build clean, **0 warnings**, **2496 expectations pass**; the app launches with the font target linked (alive after 9 s, 0 assertions, restored its board); `OFL-JetBrainsMono.txt` and `OFL-ShareTechMono.txt` are in `Contents/Resources`.
- Not verified: **that what is on screen is actually drawn in the embedded faces.** Nothing was looked at, and a silent fallback makes that an easy thing to get wrong; it is "embedded and compiled", not "seen".

## 2026-11-07 — card 5.28 (taking a pedal off the board)
- What: a filled pedal now has a small **× in its own top-right corner** (`SlotView::removeBounds()`), and the slot's right-click menu's item is renamed **REMOVE PEDAL**. Both call one path, `MainContent::removeSlotEffect`.
- The actual complaint was "once added, you are stuck until you make a new board". A `Remove` did exist, but only in the slot's context menu, and it called `BoardModel::remove`, which **erases the position and shifts everything after it down one**. That silently renumbers the board — and every macro and mapping that names a slot by index — which is why it never felt like a way to take a pedal off.
- **Decision:** removal now empties the position instead of closing the gap. New `BoardModel::clearSlot(int)`: same `Slot` object, same uid, same place, effect dropped (its params unregister) and category cleared, so the position shows its "+" again and takes the next pedal. The board's length changes only when a board is loaded or a slot is explicitly moved. `BoardModel::remove` is left alone for the load path.
- Detail: the × is hit-tested **before** the footswitch and before the drag arm, so a press there can never start carrying the pedal; it fires on release inside the button, and sliding off cancels. It is drawn dim and goes neon on hover (`mouseMove`/`mouseExit`). The status LED moved 17 px to the left so the two do not sit on each other.
- Verified: build clean, **0 warnings**, **2524 expectations pass**. `Tests/SlotClearTests.cpp` (new, 5 tests): the position survives with its uid while the pedal and category go; audio through the emptied position is dry while its neighbour still works (0.25 → 0.5, not 1.0); the pedal's `slot/<uid>/*` params are gone from the registry; an emptied position takes a new pedal; and empty/bad indices return false instead of crashing.
- Not verified: **the × itself was never clicked in the app.** A `--render-slots` render was looked at and the button is there on all three filled pedals with the LED beside it (the first render was taken from a binary built a few seconds before the link, so it showed neither — a reminder that a render is only as fresh as the app); the click, the hover and the REMOVED transient were not exercised by hand.

## 2026-11-07 — card 5.29 (a new board button)
- What: a **NEW BOARD** button at the top of the side column (above MAPPINGS), asking first when there is anything to lose and then handing an empty `BoardData` to the same `pb::apply` a preset load uses — so slots, macros and mappings clear together instead of three separate resets drifting apart. The tempo is deliberately untouched: it is the device's (card 5.25), not the board's.
- Decisions: (a) confirm only when there is a board on screen, with the message saying whether it has unsaved changes — an empty board does not need a dialog; (b) the new board is written to `current.board` straight away, so a relaunch opens the empty board rather than the one you just replaced; (c) the name goes back to untitled, so the header says NOT SAVED YET and SAVE asks for a name; (d) the selection and the editor go to the empty board rather than leaving a pedal from the old one on screen.
- A test caught my own wrong assumption here: `Slot::setEffect(nullptr)` removes the **pedal's** params but keeps `slot/<uid>/on`, because the switch belongs to the position, not the pedal. The first version of the test asserted both were gone and failed; the test now says the switch survives (an emptied position can still be switched before a pedal is put back).
- Verified: build clean, **0 warnings**, **2548 expectations pass**, 0 failed. App launches with the button (alive after 8 s, 0 assertions).
- Not verified: **the button was never clicked**, and it does not appear in a render — `--render-slots` draws the board only, not the side column, so the button's placement was checked in code and by the launch, not by eye.

## 2026-11-07 — card 5.30 (the light skin's side column)
- What: `MainContent` finally has a `paint`: it fills itself with a `style::Theme` built from `accents::machine()` and `style::isDark()`, and the four side buttons take `theme.panel2` / `theme.ink` instead of two hardcoded hexes (`0xff20202a`, `0xffb8c4dd`).
- The bug: every panel paints itself, but nothing paints the window *behind* them. The board, the macro strip, the editor and `MeterBar` cover their own rectangles; the strip at the bottom of the side column, where the four buttons sit, is covered by no one, so it showed the `DocumentWindow`'s default background — dark — under both skins. Under the light palette that read as an unpainted black bar in the bottom right.
- Decision: the window chrome uses `accents::machine()`, the same accent `MeterBar`, `TempoField` and `MacroStrip` use, so the side column matches the meters above it rather than inventing a fourth palette.
- Verified: build clean, **0 warnings**, **2548 expectations pass**, 0 failed; the app launches (alive after 8 s, 0 assertions).
- Not verified: **the light skin was not looked at.** This is a native window and there is no render path for the chrome (`--render-slots` draws the board only), so the fix is "the code now paints what it did not paint", checked by reading and by launching — not by eye. It is step 14 of REVIEW_TEMPLATE.

## 2026-11-07 — card 5.31 (TWANG was silent: two bugs, one report)
- Report: "twang is not making any noise and/or passing audio". Both halves were true, for two different reasons.
- **Bug 1, the silence.** COIL voices TWANG's chord only when a tuning value *changes*: `setSettings()` calls `chordVoices()` on `chordChange`, and `prepare()` voices it only if `configure(true)` runs with `resonator` already true. The adapter did it the other way round — `engine.prepare()` first, then `applySettingsTo()` — so prepare configured with COIL's own defaults, in which `resonator` is false and tuning/root/chord are exactly TWANG's (chord, root A, fifth). The follow-up `setSettings` then saw **no change at all**, so `chordVoices()` never ran and no voice ever rang. With MIX at 100 that is silence; below 100 it is the dry signal only, which is what made it sound like "not passing audio" rather than "a broken pedal".
- **Bug 2, found while measuring.** `Slot::process` re-pushes every parameter every block, and `setParam` set `dirty` unconditionally, so `applySettingsTo` ran every block and COIL reconfigured its springs every block. Measured: 13 dB of difference between the same input driven with and without the per-block push. Fixed by marking dirty only on a real change (floats compare with a 1e-6 tolerance; `-Wfloat-equal` is on). Same lesson as CHOP's BPM knob in card 5.22.
- Fix: apply settings **before** `engine.prepare()` (and in `reset()`), so prepare's fresh configure voices the chord; plus the change detection in `setParam`.
- **Why the existing tests missed it:** the null tests compare the adapter against a reference engine built the same way, so two silences agree bit-for-bit, and they only asserted finiteness — 0.0000000000 passed happily. The new `Tests/TwangDriveTests.cpp` measures the output instead, fully wet (MIX set before `prepare()`, so the dry path can never mask it), with and without a slot's per-block push, and requires both to be audible *and* within 0.002 of each other.
- Verified: build clean, **0 warnings**, **2553 expectations pass**, 0 failed. The null tests are bit-identical again (0.0000000000 at 64/128/511 for all three settings) after their reference was rebuilt settings-first. The pedal now rings from input: 0.0307 peak (-30.3 dBFS) from a 0.25 sine, identical with the per-block push on.
- Not verified: **how it sounds**, and whether -30 dBFS wet is the right level next to the other pedals. The number is pump's own engine at pump's own settings, so it is faithful; whether it is *loud enough* is a judgement for a guitar, not a test.

## 2026-11-07 — card 5.32 (MIDI clock, "build the midi")
- Interpretation recorded: "build the midi" = make a MIDI clock drive `board/tempo`. The rest of MIDI (notes, CCs,
  program change, learn, macros) was already there.
- New `Source/Control/MidiClock.h/.cpp`: reads system real-time bytes out of the block (`0xF8` clock, `0xFA` start,
  `0xFB` continue, `0xFC` stop) and moves `board/tempo` once per beat. 24 pulses make a beat, so the beat is the
  distance between every 24th pulse; the tempo is smoothed 0.35 per beat and clamped to the board's 20–300.
- No locks, no allocation: state is a file-scope struct (no magic-static guard on the audio thread), the registry
  index is cached per registry pointer so `find()` never runs on the audio thread, and the tempo is written only
  when it moves by 0.5 BPM — because every `set()` bumps `changeCounter()` and the working copy is saved on change.
- A gap of more than two seconds between pulses is treated as a clock that stopped, not a 0.5 BPM tempo: the beat
  restarts instead of averaging a pause into the number. `stop` resets the interval history and leaves the tempo
  where it is.
- Wired in `BoardProcessor::processBlock` immediately after `midiRouter.handle()` and before the tempo is handed to
  the pedals, so a clock change reaches them on the same block.
- Tests: `Tests/MidiClockTests.cpp` — 120 BPM, 240 BPM, 80 BPM from pulse spacing, the clamp at 300, stop resetting
  without rewinding, notes/CCs not being a tempo, a one-second gap staying inside range, and a route test that feeds
  clock bytes through `BoardProcessor` into a GATE slot and reads 120 BPM out of the pedal.
- Not verified: **a real clock**. No hardware sequencer has been plugged into this app; the route is proven with
  synthetic bytes, the wire is not.

## 2026-11-07 — card 5.33 (OVERSAMPLING waits for silence)
- The owner's instruction: "stop oversampling from happening mid play". TUBE's OVERSAMPLING changes how far the
  pedal delays the signal, and nothing compensates latency across slots, so a change under a held note shifts
  everything after that pedal (card 5.4 aligns the dry/wet fades within a slot, not across the chain).
- `PumpTubeAdapter` now keeps the request (`settings.oversampling`, so a save remembers the choice) and applies it
  only when the input has been silent for two blocks; the switch therefore lands between phrases. Other parameters
  stay live — only the one that moves the latency is held back.
- Found while testing it: `engine.latency()` reports the **current** oversampler, and the engine switches with a
  fade on its next blocks, so after `prepare()` it still reported the *old* figure (69 samples for a board saved at
  1x). `prepare()` now primes the engine with 50 ms of silence before the adapter reports latency, so delay
  compensation is right from the first block.
- Tests: `Tests/TubeOversamplingTests.cpp` — latency unchanged after 16 loud blocks with a request pending, the
  switch happening after silence, and a request made before `prepare()` being in force with the right latency.
- Not verified: how the switch sounds at a phrase boundary (nobody has listened), and whether two blocks of silence
  is the right threshold for a guitar that sustains across it.

## 2026-11-07 — card 5.34 (SPRING MODEL on TWANG "breaks the effect")
- Report: "changing the spring types in twang breaks the effect". Reproduced by measurement before touching code:
  every model rings at a fresh `prepare()` (0.049, 0.059, 0.049, 0.057, 0.048, 0.038 peak from a 0.25 burst), but
  a *live* switch measured 0.036 (three-spring), 0.028 (short tank), 0.025 (long tank), **0.00001 (digital)**,
  **0.00000 (lo-fi digital)** — the two digital models went dead.
- Cause is in COIL's `Voice::design(s, fresh)`: the digital branch computes its comb delays and loop gains and then
  returns, calling `snap()` **only when `fresh`**. `snap()` is what copies target into the smoothed `cur` values.
  A live change (`setSettings` → `configure(false)` → `v.update` → `design(s, false)`) therefore leaves the digital
  combs holding the previous model's delays: the loop does not ring, the voice's energy falls under the silence
  threshold in a few milliseconds and it deactivates. Analog models survive because `snap()` is not their only path.
- Fix in the adapter, since `external/pump` is off limits: when SPRING MODEL changes, `PumpTwangAdapter` re-voices
  TWANG's chord — two `setSettings` calls, out to LATCH tuning and straight back to chord, so `chordVoices()` runs
  and `Voice::start` → `design(s, true)` snaps the new model in. `appliedModel` tracks what the engine is really
  designed for. In LATCH/MIDI tuning nothing is re-voiced: notes drive the voices there and a chord start would be
  spurious.
- Also worth knowing: analog→analog model changes keep the *old* model's section count (`sections` is only
  recomputed when `fresh || sections < 0`), so the re-voice fixes that too — every model now sounds as designed.
- Cost, stated plainly: the ringing chord restarts, so a model change is audible as a re-pluck rather than a morph.
  That is the price of a correct design, and it beats a dead pedal.
- Tests: `Tests/TwangModelTests.cpp` — all six models measured at a fresh prepare, then a switch through 1…5 with a
  fresh strum after each, asserting each is audible (> 0.002) and finite. Before the fix models 4 and 5 failed.
  First version of the test measured after the switch *without* playing, which reads as silence for a correct
  re-voiced chord too (in resonator mode the chord is the tuning, the input is the excitation) — the test plays a
  strum after the switch, as a player would.
- Verified: build clean, 0 warnings, **2600 expectations pass**, 0 failed, null tests still bit-identical.
- Not verified: how a model change sounds in the room. The numbers say the new springs ring; nobody has heard the
  re-pluck, and whether it feels like a spring swap or a stumble is a judgement for the owner's ears.

## 2026-11-07 — card 5.35 (dependencies pinned; first push to GitHub)
- The repo (`https://github.com/matthettich/pump-board`) is the app only: `Source`, `Tests`, `docs`, `schemas`,
  181 files, ~1 MB. `build/` (1.2 GB) and `external/` (151 MB) stay ignored — a repo with build artefacts in it
  is a repo nobody can diff.
- JUCE and pump are now **submodules** at the commits this build actually uses: JUCE 9.0.5 (`d60b7a7`, shallow
  clone of master) and pump `7d981aa` (the owner's fork, `main`). `.gitmodules` records both URLs; the gitlinks
  record the SHAs. `git clone --recurse-submodules` now yields a tree that configures and builds.
- Verified by doing it: a fresh recursive clone into `build/clone-test` came back with both SHAs exact
  (`d60b7a7`, `7d981aa`), configured, built, and passed 2580 expectations with 0 failures. That
  clone is also what caught the next item.
- The fresh clone exposed a warning our Debug-only development never showed: without `PB_ALLOCATION_GUARD`,
  `pb::alloc::Scope` is an empty struct, so the guard object in `BoardProcessor::processBlock` was an unused
  local and `-Wunused-variable` fired. Marked `[[maybe_unused]]` (both build types keep the RAII intent), and
  the clone now rebuilds with 0 warnings in our code. The 20-expectation gap between 2600 (Debug) and
  2580 (plain configure) is `AllocationGuardTests` being `#ifdef PB_ALLOCATION_GUARD` — expected,
  now written into `HANDOVER.md` next to the build commands.
- Not verified: a clone on another machine. Everything above ran locally against the public remote; the
  submodule URLs are public, but nobody has cloned it from a clean Mac.

## 2026-11-06 — senior review pass (bugs) and the IDM PINK & GREEN skin
A whole-codebase review for thread-safety, lifetime and sloppiness, then the owner's reskin. Done by Claude in a
cloud session: the code was built and the suite run **on Linux** (JUCE with ALSA/JACK/XRandR/XRender off), not on
the Neo, so anything Mac-specific is unverified. Scaffold headers were changed where a real bug needed it; each one
is listed so the owner can review it (house rule: the scaffold is fixed without review).

Bugs fixed (worst first):
- **Pedal swap / remove / device change freed a pedal the audio thread was running.** `BoardModel::replace` and
  `clearSlot` called `Slot::setEffect` on a slot in the live snapshot (old `EffectPtr` destroyed mid-block, the
  `indices` vector cleared under the loop), and `prepareAll` re-prepared live slots (`dryBuffer.setSize` and every
  pedal's buffers reallocated under the callback) — despite `Slot.h` saying setEffect/prepare happen *before* a
  slot is published. Now: replace/clear build a **successor** `Slot` (`Slot::makeSuccessor`, scaffold change) that
  adopts the uid and the on switch's registry index and is published in place; the old slot runs out its block and
  is freed by the normal retire GC. `prepareAll` calls the new `BoardProcessor::detachChain` (publish an empty
  chain, wait ≤500 ms for two blocks), re-prepares, republishes. The board plays dry for a couple of blocks on a
  device change. `Slot::process` also refuses blocks bigger than it was prepared for (the gap between a device
  growing its buffer and the timer re-preparing).
  - Why the successor releases the old pedal's params early: `ParameterRegistry::add` returns the existing index
    for a known id, so `slot/<uid>/<param>` must be free before the successor registers it.
- **Mappings and macro targets survived their pedal.** Removing or swapping a pedal frees its param indices; the
  registry reuses freed indices, so a footswitch or macro silently started driving the next pedal. `MainContent::
  forgetPedalParams` prunes both before clear/replace (mappings to the slot's on switch are kept — it survives).
  `apply()` publishes an empty mapping table before `model.clear()`, and `LearnController::setMappings` cancels an
  armed learn / pending capture (it pointed at an index that may now be another pedal's).
- **Empty slots vanished on load.** `apply()` skipped empty positions instead of keeping them, so [DRIVE, -, ECHO]
  reloaded as [DRIVE, ECHO] (and an unknown pedal id, "left empty", closed the gap too). New
  `BoardModel::insertEmpty`.
- **A bypassed latency pedal jumped the signal.** Once a slot's fade settled it returned early with the undelayed
  input, so the output jumped by the pedal's latency (a click; 4096+ samples for CALM). A settled-off slot now
  outputs its dry ring at the pedal's latency, so a slot's latency is constant whether on or off and the board's
  total stays true.
- **New pedals ignored the board tempo** until the tempo next moved (the push was change-driven only). A publish
  counter now re-pushes the tempo whenever the chain changes.
- **`blocksProcessed` was a relaxed increment**; the retire GC's proof needs release ordering on ARM. Now acq_rel.
- **MIDI learn swallowed the rest of the block** (a cached `learningNow`); a clock with two ticks on one sample
  restarted the beat; a 17th held key left a Momentary mapping stuck on; the latency probe never finished on a
  device with no input; registry min/max caches were plain floats rewritten on reuse (now relaxed atomics).
- **UI:** a preset load came up starred and was written straight back (unbypass moved the change counter); two
  quick loads let the older one land on top of the newer (now latest-wins); "Type a value..." pre-filled the
  display text ("Short tank", "1/8") and Return wrote 0 or 1; "+" past the last pedal selected the still-empty
  slot clicked; Esc did not cancel a macro's Add target; several async menu/dialog callbacks captured raw `this`
  (now `SafePointer`); the min window was smaller than the fixed layout (now 1300×700); static typefaces and the
  default look-and-feel are released in `shutdown()`.
- Removed with the skin switch: the content rebuild it needed, which loaded a stale working copy before the old
  content saved (an edit in the last 800 ms was lost) and queued a callback holding a raw `this`.

Skin (owner, 6 Nov): Thunder Plus's **IDM PINK & GREEN** (`data-skin="idmpg"` in thunder-plus `beta/index.html`)
replaces the dark/light pump palettes; one skin, no switch. Paper `#E9E8E3`, panel `#F2F1EC`, panel2 `#DDDCD5`,
ink `#141414`, ink2 `#505050`, dim `#BDBCB6`, pink `#FF2D8A` (accent, playhead, "red"), green `#1F9E5A` (second).
Flat and square: 1.5 px ink outlines, no offset block. JetBrains Mono everywhere (bold for the brand); Share Tech
Mono dropped from the bundle. Category colour: pink for DRIVE/DYNAMICS/FILTER/UTIL, green for TIME/SPACE/MOD.
Meters run green, pink when hot. `IdmLookAndFeel` dresses every stock widget (menus, alert windows, the device
selector, combo boxes, text fields) so nothing falls back to JUCE's dark default. The brand reads PUMP BOARD with
BOARD in pink, as Thunder's `.brand .plus`.

Scaffold headers touched: `Slot.h` (makeSuccessor, ownsRegistrations, preparedBlock), `BoardProcessor.h`
(detachChain, deviceRunning, publishCount), `ParameterRegistry.h` (atomic caches), `BoardModel.h` (insertEmpty).
- Verified: Linux Debug build, **2628 expectations pass**, 0 failed (one existing test changed on purpose: an empty
  or unknown position now stays a position — `BoardStateTests`). New `Tests/ReviewFixesTests.cpp` pins the swap,
  clear, insertEmpty, prepareAll, tempo-on-insert, learn-mid-block and bypassed-latency fixes. Offscreen renders
  (`--render-slots`, `--render-editor` for TUBE and ECHO) under Xvfb show the new skin.
- Not verified: anything on the Neo — clang's warnings, CoreAudio, a real device change mid-play (the
  `detachChain` wait), swapping a pedal while playing, how the skin looks at Retina scale, and the Settings
  overlay's device selector in the new look-and-feel (no device on the Linux box, so it was never rendered).

## Open items 1, 3, 4, 6, 7, 8 from the 6 Nov review (pass of 6 Nov, Linux)

- **Item 1, dirty only on change.** New `Source/Effects/adapters/AssignHelpers.h` (`assignField (field, value, dirty)`,
  exact compare written with `<`/`>` so `-Wfloat-equal` stays quiet). Every adapter's `setParam` (BAND, CALM, CHOP,
  COMP, CRUSH, ECHO, EQ, GATE, GRAIN, HEAT, LIMIT, SNAP, SPRING, STRIP, STUTTER, SWIRL, TONE, TUBE, VERB, WIDE) uses it;
  `prepare()`/`reset()` still leave `dirty = true`, so the change pending after prepare (the setSettings landmine) is
  unchanged. TWANG was left as it was. Null tests are unchanged (difference 0.0 everywhere except WIDE's balanced
  two-band case, which is the known exception and varies run to run with the same binary).
  TUBE oversampling: a request back to what the engine already runs now also drops a stale pending request; and since
  other params no longer re-push `settings.oversampling` every block, the wait-for-silence rule (card 5.33) now holds
  in a live slot (before, any dirty param pushed the new oversampling at once). Not verified by ear.
- **Item 3.** `Slot::maxDelayFor` / `BoardProcessor::maxDryDelayFor`; `Slot.h` and `BoardProcessor.h` (scaffold)
  gained a static, a getter and one int member each. Allocation stays in `prepare` / `prepareToPlay`.
- **Item 4.** Editor, macro strip and meters repaint when what they draw changed. `MainContent::syncSlots (false)` on
  the tick skips when `changeCounter()` and a hash of the chain (slot and pedal pointers, count) are unchanged;
  every explicit caller still forces it.
- **Item 6.** `GenericEffectEditor::resized` tries one to three balanced rows and keeps the one with the biggest knob
  whose names still fit; a dense pedal still under 40 px borrows 8 px from under the caption. Checked by `--render-editor`
  for ECHO and TUBE only (Linux, 1100 px wide).
- **Item 7.** `fileFor` falls back to "Untitled" and strips leading dots; case-only rename goes through a hidden temp
  file; `rename` undoes its new copy when the Bin refuses; `PresetManager::setTrashFunction` exists so a test can make
  it refuse. The case-insensitive path itself (macOS) is exercised on Linux only through the same temp-name route.
- **Item 8.** `MidiRouter`'s side table is a push-only linked list of `RouterState` nodes (freed at static teardown),
  so any number of routers get a retire list; the audio thread only walks it with acquire loads. A use-after-free is not
  observable from a test, so `Tests/OpenItemsTests.cpp` pins the behaviour with twenty live routers.
- Tests: new `Tests/OpenItemsTests.cpp`.

## 2026-11-06 — new pedals and the paged editor (verified by Claude)
- pump TUNE, VOX and GRUMBLE added as adapters (category "voice"), pump pinned to `c0da8c7` for GRUMBLE (upstream
  only added that plugin since `7d981aa`). Null tests bit-identical at 64/128/511. AMP deliberately not added: see
  HANDOVER "Still open" item 3.
- The editor pages dense pedals (≤20 controls a page, page keys top right, groups from the param id prefix; a group
  labels a page only when it has 3+ controls there). `--render-editor out.png 1.0 pump.vox 2` renders page 2.
- Category colours compare case-insensitively: SWIRL's "modulation" was falling through to pink.
- Verified: 3204 expectations pass on Linux; renders of VOX (3 pages), GRUMBLE, TUNE, ECHO, TUBE looked at.
- Not verified: anything on the Mac, page keys clicked live, how the three voice pedals sound on a guitar.
