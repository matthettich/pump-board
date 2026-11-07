# Phases 5–7

These phases get re-planned with Claude after the Phase 4 review, once it's known how pump's DSP and the engine behave. The cards below are the starting outline.

---

# Phase 5 — The rest of pump's effects

## Card 5.N — One adapter per effect

Repeat card 1.3 for each "separable" effect in `docs/PUMP_DSP_MAP.md`, one card and one commit each. Same null test at block sizes 64, 128 and 511.

## Card 5.X — Per-slot CPU meter

**Files:** `Source/Engine/Slot.cpp` (timing only), `Source/UI/SlotView.cpp`.
Time each slot's `process` with `juce::Time::getHighResolutionTicks()` (no allocation), store a smoothed fraction of the block's time budget in an atomic, and show it as a thin bar on the SlotView. Show the whole board's load in the status bar.

Check: the full board's load at 64 samples is recorded in the review.

---

# Phase 6 — Custom pedals (modular slots)

## Card 6.1 — Utility nodes
`Source/Modular/nodes/`: Split (1 in → 2 out), Mix (2 in → 1 out with crossfade), Gain, Filter (LP/HP/BP, `juce::dsp::StateVariableTPTFilter`), LFO (sine, triangle, square, random; rate in Hz or synced later), EnvFollower (attack, release), In, Out. Each is an `IEffect` with ports described by a small `NodePorts` struct (audio ins, audio outs, mod outs). Tests per node.

## Card 6.2 — Compiler
Implement `compile()` in `PedalGraph.h`: validate (exactly one In and one Out, ports in range, every fbsend has a matching fbreturn), topologically sort audio wires with feedback pairs cut, assign one preallocated stereo buffer per output port (reuse buffers whose readers have all run), and emit a flat run list. Tests: a straight line, a split/mix diamond, an illegal cycle (clear error), a legal feedback loop, 32 nodes.

## Card 6.3 — PedalEffect
`Source/Modular/PedalEffect.h/.cpp`: an `IEffect` (`typeId` "pedal.custom") that runs a published `PedalRenderPlan`, swapped and retired like a ChainSnapshot. Its `params()` are the pedal's face knobs. Modulation wires are applied once per block (control rate).

## Card 6.4 — PedalSerializer and the pedal library
`.pedal` JSON in `~/Music/pump BOARD/Pedals/`, same rules as BoardSerializer. A board slot stores the pedal's relative path. A missing pedal file → empty slot + warning.

## Card 6.5 — Patcher UI
`Source/UI/PatcherView.h/.cpp`: nodes as boxes with ports, drag to wire, right-click to delete, a node palette, a "face" panel to pick up to 8 knobs to expose. Recompile and publish on every change. Show compile errors in place, never a modal dialog.

Check: a two-path pedal (split → delay on one side, filter on the other → mix) works in a board slot and saves and loads.

---

# Phase 7 — Polish

- Skin in pump's style: a dark background, each pedal edge in its category's light neon colour, dense but simple controls (SPRING's density as the model).
- Overload protection: if the board's load stays above 90% for 2 s, show a warning and suggest the 128 buffer. Never auto-bypass silently.
- Packaging: a release build workflow on GitHub Actions for macOS arm64. Signing and notarization if the Apple Developer account exists.
- Final check: one hour of playing through a full 16-slot board at 128 samples with no glitches or thermal throttling.
