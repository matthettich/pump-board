# Phase 2 — The board

Goal: 16 slots you can fill, reorder and switch while playing, with no clicks and no audio-thread allocations.

---

## Card 2.1 — Slot

**Files:** `Source/Engine/Slot.cpp`, `Tests/SlotTests.cpp`.

Steps (follow the comments in `Slot.h`):
1. `setEffect` registers `slot/<uid>/<paramId>` for each param (with the effect's defaults) and `slot/<uid>/on` (toggle, default 1). Unregister old params first if replacing.
2. `prepare` sizes `dryBuffer` to 2 × maxBlock and resets `wetGain` with a 10 ms ramp, then calls `effect->prepare`.
3. `process`: push targets, copy dry, run the effect, then per sample `out = dry * cos(g·π/2) + wet * sin(g·π/2)` with `g = wetGain.getNextValue()`. If the effect is null, return untouched.
4. Optimisation: when fully bypassed (`g == 0` and not ramping), still call `effect->process` on a scratch copy only if the effect has a tail and you want tails to ring out. **v1: skip processing while fully bypassed**, and note it in NOTES.md.

Tests: a slot with a gain-only test effect (write a tiny `TestGainEffect` in the test file) — on gives gain, off gives dry, switching produces a 10 ms ramp with no sample step larger than 0.05 for a DC input.

---

## Card 2.2 — BoardModel and snapshot publishing

**Files:** `Source/Engine/BoardModel.h/.cpp` (new, message thread), `Source/Engine/BoardProcessor.cpp`, `Tests/BoardModelTests.cpp`.

Steps:
1. `BoardModel` holds `std::vector<std::shared_ptr<Slot>> slots` (max 16) and has `insert(index, typeId)`, `remove(index)`, `move(from, to)`, `replace(index, typeId)`, `clear()`. Each new slot gets a uid from `juce::Uuid`.
2. After every change: build a `ChainSnapshot` from `slots` (new slots are prepared first using the processor's sample rate and block size), then `processor.publish(...)`.
3. `BoardProcessor::publish`: `old = active.exchange(next.release())`, then `retireList.push_back({old, blocksProcessed.load() })`.
4. `collectGarbage`: delete retired snapshots whose `retiredAtBlock + 2 <= blocksProcessed`. Run it from a 30 Hz `juce::Timer` in MainWindow.
5. `processBlock` runs `active`'s slots in order.

Tests: insert 16, the 17th fails; move keeps the same Slot object; publish and then collectGarbage with a fake block counter frees the old snapshot exactly once (count destructor calls with a test hook).

---

## Card 2.3 — Board bypass and safety limiter

**Files:** `Source/Engine/BoardProcessor.cpp`, `Source/Engine/SafetyLimiter.h/.cpp`.

Steps: board bypass crossfades (10 ms, equal power) between the processed chain and the trimmed input. Replace the hard clip with a simple look-ahead-free peak limiter (instant attack, 50 ms release, ceiling −0.3 dBFS). It is always on and not mappable.

Checks: a 16-slot board of gain effects at +12 dB each never exceeds −0.3 dBFS out.

---

## Card 2.4 — AllocationGuard

**Files:** `Source/Common/AllocationGuard.cpp`, `Tests/AllocationGuardTests.cpp`.

Steps:
1. A `thread_local int depth` raised by `Scope`.
2. In Debug builds (`PB_ALLOCATION_GUARD`), override global `operator new`, `new[]`, `delete`, `delete[]` (all sized and aligned variants) to call `recordViolation()` when `depth > 0`, then forward to `malloc` / `free`.
3. `processBlock` opens a `Scope` for its whole body.
4. MainWindow shows a red "ALLOC n" badge in Debug builds when `violations() > 0`.

Tests: allocating inside a Scope counts 1; outside counts 0; a full `processBlock` on a 16-slot board of the three Phase 1 effects counts 0.

---

## Card 2.5 — Board UI

**Files:** `Source/UI/BoardView.h/.cpp`, `Source/UI/SlotView.h/.cpp`, `Source/UI/EffectPicker.h/.cpp`.

Steps:
1. BoardView: 2 rows of 8 SlotViews (fits the Neo screen), signal flow left to right, top row then bottom row, with a small arrow between rows.
2. SlotView: effect name, a big on/off footswitch (bound to `slot/<uid>/on`), a coloured edge per category, an empty slot shows "+".
3. Click "+" opens EffectPicker (a list from `EffectRegistry::list()`, grouped by category, with a search box). Right-click a slot: Replace / Remove / Duplicate.
4. Drag a slot onto another position to move it (`BoardModel::move`).
5. Selecting a slot shows its editor (GenericEffectEditor or the effect's own) in the bottom half of the window.

Checks: reorder and toggle while playing produce no clicks; the ALLOC badge stays off.

**End of phase: fill in REVIEW_TEMPLATE.md and bring it back.**
