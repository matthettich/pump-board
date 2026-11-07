# Phase 4 — Presets

Goal: save and load whole boards, and switch between them from MIDI or keys.

---

## Card 4.1 — BoardSerializer

**Files:** `Source/Presets/BoardSerializer.cpp`, `Tests/BoardSerializerTests.cpp`.
**Give the model:** `BoardSerializer.h`, `schemas/board.schema.json`.

Steps:
1. `toJson`: use `juce::DynamicObject` / `juce::var` and `juce::JSON::toString`. Keys exactly as in the schema. Always write `schemaVersion`.
2. `fromJson`: parse with `juce::JSON::parse`. Missing optional fields get defaults. A `schemaVersion` newer than `kSchemaVersion` returns `ok = false` with a clear error. More than 16 slots or 8 macros: keep the first ones and add a warning.
3. No exceptions, no asserts on bad input.

Tests: round trip (data → json → data → json gives identical strings); every optional field missing; garbage input returns `ok = false`; 20 slots gives 16 and a warning.

---

## Card 4.2 — Board ↔ BoardData

**Files:** `Source/Presets/BoardState.h/.cpp`, `Tests/BoardStateTests.cpp`.

Steps:
1. `capture(BoardModel&, MacroBank&, mappings, ParameterRegistry&) -> BoardData`. Slot param values are read from the registry by id. Mapping targets are written as `slot/<position>/<paramId>`, not uid.
2. `apply(const BoardData&, ...) -> LoadResult`: clears the board, creates each slot from its typeId (an unknown id → empty slot + warning), sets params, macros and mappings, then publishes one snapshot and one mapping table.

Tests: capture → apply → capture gives equal BoardData; an unknown effect id gives an empty slot and a warning.

---

## Card 4.3 — Preset manager and UI

**Files:** `Source/Presets/PresetManager.h/.cpp`, `Source/UI/PresetBar.h/.cpp`.

Steps:
1. Presets live in `~/Music/pump BOARD/Boards/*.board`. The list is sorted by name, and its order is the program-change number.
2. PresetBar at the top: preset name, ◀ ▶, Save, Save As, a dropdown list. Shows a dot when the board has unsaved changes.
3. ControlEvents from MidiRouter (`PresetSelect/Next/Prev`) and keys call the manager. Loading a preset while playing must not click: ramp the board's output down over 10 ms, swap, ramp up. Use the board bypass smoother or a dedicated one.
4. Warnings from loading show in a dismissable bar, never a modal dialog.

Checks: switching presets by MIDI program change while playing is click-free; the last used preset reopens on launch.

**End of phase: fill in REVIEW_TEMPLATE.md and bring it back. At this point the board is playable.**
