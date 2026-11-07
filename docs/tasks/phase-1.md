# Phase 1 — Effect interface and the first three pump adapters

Goal: prove pump's DSP can be driven from pump BOARD, with output identical to pump's own plugins.

---

## Card 1.1 — Map pump's DSP (investigation, no app code)

**Files:** `docs/PUMP_DSP_MAP.md`, `CMakeLists.txt` (`PUMP_DSP_SOURCES` only).
**Give the model:** the directory listing of `external/pump` (`find external/pump -name '*.h' -o -name '*.cpp' | head -300`) and the headers you ask it to open.

Steps:
1. For every pump effect (not instruments, not ROOM, CONVERT or AMP), find:
   - the DSP class name and its header path
   - whether it can be constructed and run without its `AudioProcessor` or editor
   - its parameter list: id, range, default (from the plugin's parameter layout)
   - its prepare / reset / process signatures
2. Write the result as a table in `docs/PUMP_DSP_MAP.md`, one row per effect, with a "separable?" column: yes / needs a small refactor / no.
3. Add the `.cpp` files of the three Phase 1 effects (suggested: SPRING, the delay, the tube saturator) to `PUMP_DSP_SOURCES`.

Checks:
- The map covers every effect in the repo.
- The build still succeeds with the three DSP sources added.
- **Stop here and bring the map to Claude** if any of the three is "needs a refactor" or "no". The refactor happens in the pump repo, reviewed separately.

---

## Card 1.2 — EffectRegistry

**Files:** `Source/Effects/EffectRegistry.cpp`, `Tests/EffectRegistryTests.cpp`.

Steps: implement the header. `list()` sorts by category, then display name. `create()` of an unknown id returns nullptr and logs nothing (it may be called while loading a file).

Tests: add then create returns a non-null effect with the right typeId; unknown id returns nullptr; list is sorted; a duplicate add asserts (use `expectThrows` only if you change the assert to a throw in Debug; otherwise test that `contains` stays true and skip duplicate behaviour).

---

## Card 1.3 — First adapter (do one, then copy the pattern)

**Files:** `Source/Effects/adapters/PumpSpringAdapter.h/.cpp` (or whichever effect is first), `Source/Effects/BuiltInEffects.cpp` (`registerBuiltInEffects`), `Tests/AdapterNullTests.cpp`.
**Give the model:** `AdapterTemplate.h`, `IEffect.h`, the effect's row from `PUMP_DSP_MAP.md`, and pump's DSP header for that effect.

Steps:
1. Copy `AdapterTemplate.h` and fill it in for the effect. Param ids, ranges and defaults match pump's plugin exactly.
2. Register it as `pump.<name>` with a category.
3. Null test: render 2 seconds of a fixed test signal (a 100 ms sine burst at 440 Hz, then a 2 s noise burst at −12 dBFS, seeded) through (a) the adapter and (b) pump's DSP class driven directly with the same settings. Subtract. The difference must be below −90 dBFS peak.
4. Run the test at block sizes 64, 128 and 511, to catch block-size bugs.

Checks: null test passes at all three block sizes.

---

## Card 1.4 — Adapters two and three

Same as 1.3 for the other two effects. One card per adapter if the model struggles.

---

## Card 1.5 — Generic effect editor

**Files:** `Source/UI/GenericEffectEditor.h/.cpp`.

Steps: a component that lays out one rotary knob per `ParamInfo` (a toggle button for `isToggle`), bound to registry indices passed in. Name under each knob, value and unit on hover. A double-click resets to default (matching Thunder Plus's mouse rules).

Checks: opening any of the three effects shows its knobs, and turning them changes the sound.

**End of phase: fill in REVIEW_TEMPLATE.md and bring it back.**
