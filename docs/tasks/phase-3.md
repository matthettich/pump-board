# Phase 3 — Control: MIDI, keys and macros

Goal: every switch and knob is mappable to a MIDI note, CC, program change or computer key, plus 8 macro knobs.

---

## Card 3.1 — MidiRouter core

**Files:** `Source/Control/MidiRouter.cpp`, `Tests/MidiRouterTests.cpp`.

Steps:
1. `publish` / `collectGarbage` work like BoardProcessor's (retire by block count; MidiRouter needs its own counter, bumped in `handle`).
2. `handle`: for each message, build a `MappingSource`. If `learning`, push `LearnCaptured`, clear `learning`, and skip acting. Otherwise scan the table (linear scan is fine, under 200 mappings) and apply every match.
3. Actions:
   - Toggle: note-on velocity > 0 → `registry.toggle`.
   - Momentary: note-on → set max, note-off (or velocity 0) → set min.
   - CCThreshold: value ≥ 64 → max, else min.
   - SetValue: `min + (value / 127) * (max - min)`.
   - PresetSelect / Next / Prev: push a ControlEvent.
4. Channel 0 in a mapping matches any channel.

Tests: build a table by hand and feed MidiBuffers. Cover each action, the channel filter, learn capture, and a full FIFO (it drops events rather than blocking).

---

## Card 3.2 — Keys

**Files:** `Source/Control/KeyRouter.h/.cpp`, `Source/App/MainWindow.cpp`, `Source/Control/MidiRouter.cpp` (`handleKey`).

Steps: MainWindow forwards `keyPressed` and `keyStateChanged` to KeyRouter, which tracks which keys are down (for Momentary) and calls `MidiRouter::handleKey`. Ignore keys while a text field has focus. Preset actions from keys go to the preset manager directly.

Checks: a key mapped to Toggle flips a slot; a key mapped to Momentary holds it on only while pressed.

---

## Card 3.3 — Learn UI and mapping list

**Files:** `Source/UI/LearnController.h/.cpp`, `Source/UI/MappingsView.h/.cpp`, edits to SlotView, GenericEffectEditor and the macro strip.

Steps:
1. Right-click any mappable control → Reset / Type / Learn (Thunder Plus's menu). Learn: highlight the control, call `beginLearn()`, wait for `LearnCaptured` (or a key press), then ask the action with a small popup (Toggle / Momentary / Threshold / Value), defaulting to Toggle for switches and Value for knobs.
2. Build a new MappingTable from the model's mapping list plus the new one and publish it.
3. MappingsView lists every mapping (source, action, target, range) with delete and edit.
4. Esc cancels learn.

Checks: learning a footswitch from a MIDI note and from a key both work; mappings show in the list.

---

## Card 3.4 — MacroBank

**Files:** `Source/Control/MacroBank.cpp`, `Source/UI/MacroStrip.h/.cpp`, `Tests/MacroBankTests.cpp`.

Steps:
1. Implement the header. `mapValue`: Linear `min + v·(max−min)`; Exponential uses `v²`; Logarithmic uses `sqrt(v)`; Inverted uses `1 − v`.
2. MacroStrip: 8 knobs along the bottom of the window, each with an editable name. Right-click a macro → "Add target": then click any knob in a slot editor to add it as a target, with the current value as both min and max, adjustable in a small popup.
3. `update()` runs on the 30 Hz timer.

Tests: `mapValue` for each curve at 0, 0.5 and 1; a macro with two targets writes both; moving the macro param via the registry (like MIDI would) is picked up by `update()`.

**End of phase: fill in REVIEW_TEMPLATE.md and bring it back.**
