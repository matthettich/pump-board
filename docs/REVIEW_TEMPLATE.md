# pump BOARD — review of phases 0–4

Written 2026-11-07, after card 4.3. This is the document the plan asks for at the end of each phase: what
each card actually delivered, what proves it, and what a human still has to try. It is a record, not a plan —
the plan stays in `docs/tasks/`.

**State at the time of writing:** build clean, 0 warnings in our code, **2496 expectations, 0 failing**. The app launches, restores three pedals, saves a `.board` working copy and reopens it.

**What this review is not.** Nothing in here has been clicked. Everything below is argued from the code, from
the test output, or from renders that were looked at. The hand-test checklist at the end is the part that
cannot be done from a terminal.

---

## Phase 0 — Skeleton and passthrough

| Card | Status | Evidence |
| --- | --- | --- |
| 0.1 Project builds | Done | `CMakeLists.txt` + Ninja; app opens a window; `Tests/TestMain.cpp` runs the suites |
| 0.2 AudioEngine + device settings | Done | `AudioEngine`/`SettingsView`; `audio.xml` restores the device |
| 0.3 Passthrough, meters, registry | Done | `BoardProcessor` chain, `MeterBar`, `ParameterRegistryTests` (6) |
| 0.4 Latency readout | Done | Reported latency in the status line and in Settings; `LatencyProbe` (5 tests); the measured number shows in Settings |

- 0.1 deviation: the plan suggested `-G Xcode`; the project builds with Ninja and `cmake --build build`, which
  is what every BUILD section here assumes.
- 0.3: the card's hard clip was replaced by the real limiter in card 2.3 (`SafetyLimiter`, 7 tests).
- 0.4 was the one gap in phase 0 and is now built: `LatencyProbe` sends one click and finds it coming back,
  and Settings has the button, a hint line and the result. The probe rides the device as a second callback
  rather than changing `BoardProcessor`'s fixed header, so the click is measured unprocessed.
  What a terminal cannot do is prove it: the tests measure a synthetic click 96 samples away, but no cable has
  been patched into this machine. `HANDOVER.md` still carries the reported-vs-measured wording as a decision.

## Phase 1 — Effect interface and the first three adapters

| Card | Status | Evidence |
| --- | --- | --- |
| 1.1 Map pump's DSP | Done | `docs/PUMP_DSP_MAP.md`, 25 effects, "separable?" column |
| 1.2 EffectRegistry | Done | `EffectRegistryTests` (4) |
| 1.3–1.4 Three adapters | Done | `pump.spring`, `pump.echo`, `pump.tube`; `AdapterNullTests` (3) + `EchoTubeNullTests` (7) |
| 1.5 GenericEffectEditor | Done | builds a panel from any `params()`; used by every selected slot |

- Null tests are the strong claim here: adapter vs pump's own DSP class, difference **exactly 0.0** at 64, 128
  and 511 samples, default and edited. That is bit-identical, not "close".
- 1.5 grew beyond the card in phase 3: right-click Reset / Type a value / Learn, and the pick-a-control mode
  that a macro's "Add target" uses.

## Phase 2 — The board

| Card | Status | Evidence |
| --- | --- | --- |
| 2.1 Slot | Done | 10 ms equal-power crossfade, registry per param; `SlotTests` (4) |
| 2.2 BoardModel + snapshots | Done | retire-at-N+2; `BoardModelTests` (7) |
| 2.3 Bypass + safety limiter | Done | `BoardProcessorTests` (5), `SafetyLimiterTests` (7); 16 × +12 dB lands at −0.30 dBFS |
| 2.4 AllocationGuard | Done | `AllocationGuardTests` (4); a full block on a 16-slot board allocates **0** |
| 2.5 Board UI | Done with deviations | 16 real `SlotView`s over `BoardPrototype`; drag to reorder; category-grouped picker |

- 2.1 deviation (recorded in `NOTES.md` at the time): a fully bypassed slot is not processed, so a tail does
  not ring out through an off pedal.
- 2.2 deviations: slot uids are `s1, s2, …` from a counter, not `juce::Uuid`; the 16 limit is
  `ChainSnapshot::kMaxSlots`.
- 2.5 deviations: there is no separate `BoardView` or `EffectPicker` component. The board is
  `BoardPrototype` (painted chrome, hit-testing, drag) with `SlotView`s on top, and the pedal picker is a
  `PopupMenu` grouped by category. The card's search box and its "Duplicate" slot item do not exist; the slot
  menu offers Replace / Remove / MAP THIS SWITCH.

## Phase 3 — Control: MIDI, keys and macros

| Card | Status | Evidence |
| --- | --- | --- |
| 3.1 MidiRouter core | Done | all seven actions, channel filter, learn, full FIFO drops; `MidiRouterTests` (13) |
| 3.2 Keys | Done | hold tracking, auto-repeat swallowed, text-field guard; `KeyRouterTests` (8) |
| 3.3 Learn UI + mapping list | Done with deviations | `LearnController` (8), `MappingsView`, editor and slot menus |
| 3.4 MacroBank + strip | Done with gaps | curves, two targets, update-on-change; `MacroBankTests` (5); real strip renders |

- 3.1 deviations (all in `NOTES.md`): the fixed header has no block counter or retire list, so both live in a
  per-instance table inside the `.cpp`; the FIFO holds 255 events; Toggle fires only from note-on or key press,
  so a CC mapped to Toggle does nothing; Threshold and SetValue are CC-only; preset actions fire on "press".
- 3.2 deviations: released keys are found by polling `KeyPress::isKeyCurrentlyDown` from `keyStateChanged`,
  because JUCE does not say which key changed; preset actions from keys are returned directly rather than
  pushed through the audio FIFO.
- 3.3 deviation: `LearnController` lives in `Source/Control`, not `Source/UI` as the card says, so the core
  test target can prove the handshake.
- 3.4 gaps: no per-target view (a target's range and curve cannot be edited after it is added), and a macro
  knob cannot be learned onto MIDI or a key from the UI — the registry param exists, the menu item does not.

## Phase 4 — Presets

| Card | Status | Evidence |
| --- | --- | --- |
| 4.1 BoardSerializer | Done | schema JSON both ways; `BoardSerializerTests` (6) |
| 4.2 Board ↔ BoardData | Done | position-based targets, dropped-target warnings; `BoardStateTests` (7) |
| 4.3 Preset manager + UI | Done with deviations | `PresetManager` (5), `WarningBar`, header name + star, bypass fade |

- Verified end to end by launching twice: the first launch imported the old `board.xml` and wrote
  `~/Music/pump BOARD/current.board`; the second logged "restored 3 pedal(s), 0 mapping(s)".
- 4.2 deviation: `apply` publishes per slot, because `BoardModel::publish()` is private and the header is
  fixed; the preset fade hides it.
- 4.3 deviations: no separate `PresetBar` widget (the header's `<` `>` `SAVE` and the PRESETS menu are the
  bar); the fade borrows the board bypass instead of a dedicated smoother; one pending swap, so a burst of
  program changes keeps only the last.
- Gap raised for the owner: `BoardData` has no tempo, so a saved board does not restore its BPM.

## Phases 5–7 — where they stand

- **Phase 5 (rest of pump's effects): in progress.** Thirteen done: `pump.crush` (`CrushNullTests`, 12),
  `pump.swirl` (`SwirlNullTests`, 15 — the 3 ms through-zero variant and a tempo-synced phaser included),
  `pump.comp` (`CompNullTests`, 15 — lookahead, 2x oversampling, feedback topology, mid/side) and `pump.heat`
  (`HeatNullTests`, 15 — 8x oversampling, linear-phase crossovers, per-band curves) and `pump.tone`
  (`ToneNullTests`, 12 — STFT EQ, 43/85 ms latency, the quality switch included), `pump.verb` (`VerbNullTests`,
  12 — 12 reverb modes, zero latency) and `pump.limit` (`LimitNullTests`, 12 — true peak, dither, 12 setters behind
  one `applySettingsTo()`), `pump.snap` (`SnapNullTests`, 12 — transient designer, lookahead behind a fade),
  `pump.strip` (`StripNullTests`, 12 — console strip, 8 models, 4-band EQ, gate, comp) and `pump.calm`
  (`CalmNullTests`, 12 — resonance catcher, FFT latency), `pump.gate` (`GateNullTests`, 12), `pump.grain`
  (`GrainNullTests`, 12) and `pump.stutter` (`StutterNullTests`, 12) — the last three tempo-locked, free-running at
  the adapter's own 120 BPM. All exact, 0.0 peak difference at 64/128/511, dither included. The pattern is settled: `AdapterTemplate.h` + a row of `PUMP_DSP_MAP.md` + a
  null test. Nine effects to go. pffft is linked, so the FFT-based ones are no longer a special case. **Cards 5.4 and 5.5 came from the owner's request rather than the plan:**
  latency alignment (both dry paths now run through delay rings keyed to the reported latency, so a pedal's
  on/off and the board's bypass no longer comb — and card 5.6 puts that total in the status line) and the
  light skin (Settings → SKIN, session-only). The per-slot CPU meter is also not started.
- **Phase 6 (custom pedals): not started.** `Source/Modular/PedalGraph.cpp` is a stub marked `TODO card 6.2`.
- **Phase 7 (polish): partial.** The skin is real — `PumpStyle` neons, the painted board, dense dials — and it
  drives the actual UI. Missing: the overload warning above 90% load, any packaging/CI, signing, and the
  one-hour soak.

---

## Hand-test checklist

In the order worth trying it. Each line says what should happen; if it does not, that is a bug, not a gap.

1. **Launch.** Three pedals are on the board, the status line shows the device, `< >  SAVE  BYPASS` are in the
   header. No warning bar.
2. **Footswitches.** Click a pedal's switch: the LED, the neon edge and the sound change. Click it again.
3. **Knobs.** Select a pedal, turn a knob: the value on the pedal's silkscreen follows, and the star appears in
   the header. Double-click the knob: it returns to its default.
4. **Learn from a key.** Right-click a knob → Learn, press `q`: the action popup appears with Value ticked.
   Choose it; the MAPPINGS button shows the row. Press `q` and watch the knob move.
5. **Learn from MIDI** (footswitch plugged in): the same, but press the switch instead. Then play the switch
   with the app focused and confirm it drives the target and nothing else.
6. **Macro.** Right-click a macro → Add target, click a knob on a pedal, accept the popup. Turn the macro: the
   target moves and the pedal's value display follows. Rename the macro; the name on the strip changes.
7. **Reorder.** Drag a pedal to another position while playing. The sound stays continuous and the ALLOC badge
   (Debug) stays off.
8. **Save As.** PRESETS → SAVE THIS BOARD AS…, name it "Test board". Change a value, press SAVE. PRESETS shows
   it; the star goes away.
9. **Relaunch.** Quit and open the app: the changed value is there, the header shows "TEST BOARD", and no star.
   This is the whole of phase 4 in one step.
10. **Load while playing.** With a note ringing, load another preset. Listen for a click — this is the one
    claim that cannot be settled from code.
11. **Bad file.** Save a board, break its JSON in a text editor, load it. A dismissable bar explains; the app
    keeps playing the board it had.
12. **Measure the round trip.** Patch an output back into an input, then Settings → MEASURE ROUND TRIP. A
    number appears within a second; with nothing patched the line says CHECK THE LOOPBACK CABLE. Compare it
    with the reported figure above it — the gap is the interface's real converter overhead.
13. **Ten minutes** at 64 samples with a full board: no dropouts, no ALLOC badge, meters sane.
14. **Flip the skin.** Settings → SKIN: the window rebuilds in the light palette and keeps its size and position.
    Every panel should change — board, slots, macro strip, meters, editor, and the side column with the four
    buttons (card 5.30: that strip used to stay dark). If a dark rim still shows around the outer edges, that is
    the window frame's own background, a known gap.
15. **The tempo is the device's.** Set 97 BPM, load another board, quit and reopen the app. Still 97, on the
    other board too. It is not in any `.board` file — that is the design, not a lost setting.
16. **New board.** Click NEW BOARD: it asks first if a board is loaded, then gives sixteen empty "+" positions,
    UNTITLED in the header, no macros and no mappings — and the tempo you set is still there. Quit and reopen:
    the empty board is what comes back.
17. **Take a pedal off.** Click the small × in a pedal's top-right corner: it empties that position, the slot beside
    it does not jump into its place, and the "+" takes a new pedal. Right-click the same pedal → REMOVE PEDAL does
    the same thing. With a guitar plugged in the emptied position should be silent (dry), not muted.
18. **Rename and delete.** PRESETS → hover a board → RENAME... gives it a new name (menu and header follow);
    DELETE... asks first, then the file is in the Bin. SAVE afterwards still overwrites the renamed board
    rather than making a second copy.
15. **Read the status line.** With COMP (lookahead up) or CRUSH on the board it should end `BOARD 6.0 ms` or
    similar; with nothing latency-reporting on the board the field should be absent, not `0.0 ms`.
16. **Bypass with a latency pedal on the board.** Put COMP (with 2 ms lookahead) or CRUSH on slot 1, play, and
    press the board bypass. There should be no hollow, phasey drop in the crossfade, and switching the slot
    itself off should not comb either.

Not testable yet: renaming or deleting a preset in the app, and the tempo coming back with a board.

## Summary of what is missing versus the plan

- The card 2.5 searchable pedal picker and "Duplicate"; the separate `BoardView`/`EffectPicker` files.
- A per-target macro view, and Learn on a macro knob (card 3.4's gaps).
- Rename/delete presets; a per-board tempo (needs a schema decision).
- Phases 5 and 6 in full; phase 7's overload warning and packaging.
