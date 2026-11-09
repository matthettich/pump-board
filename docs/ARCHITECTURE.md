# pump BOARD — architecture (condensed)

Mac-only JUCE standalone app (JUCE 9.0.5, a submodule). Audio in → input trim → up to 16 slots in series → output trim → safety limiter → audio out. 48 kHz, 64-sample buffer (128 fallback). Target round trip about 4–8 ms on a MacBook Neo (A18 Pro, 8 GB, fanless).

## Threads

| Thread | Runs | May allocate/lock? |
| --- | --- | --- |
| Audio | `BoardProcessor::processBlock`, every `Slot`, every `IEffect::process`, MIDI handling | No. Never. |
| Message (UI) | UI, editing, snapshot building, presets, key handling | Yes |

Audio and message threads share only:
1. `std::atomic<ChainSnapshot*>`: the chain the audio thread plays.
2. `ParameterRegistry`: a fixed array of `std::atomic<float>` values.
3. Lock-free FIFOs for events going from audio to message (e.g. "MIDI asked for the next preset").

## The chain snapshot (the most important rule)

- The audio thread plays exactly one `ChainSnapshot`: an immutable list of up to 16 `Slot*`.
- To change structure (add, remove, reorder, load a preset), the message thread:
  1. builds a new `ChainSnapshot`, preparing any new slots there with `prepare()`
  2. publishes it with `BoardProcessor::publish()` (an atomic exchange)
  3. keeps the old one in a retire list, and frees it only once the audio thread has started two more blocks, which is when the block that might still have been reading it has finished (`BoardProcessor::collectGarbage`; with no device running nothing is reading, so it frees at once).
- `Slot`s are owned by the message-thread `BoardModel` through `std::shared_ptr`. A slot that moves position is the same object, so delay tails and state survive reordering.

## Values and control

- Every controllable value lives in `ParameterRegistry` under an id: `slot/<slotUid>/<paramId>`, `board/bypass`, `board/inputTrim`, `board/outputTrim`, `board/tempo`, `macro/<n>`.
- Writers (UI, MIDI, keys, macros) set the target atomically. `Slot` reads targets once per block and passes them to its effect, which smooths them.
- `MidiRouter` runs on the audio thread at the start of each block. It looks up the incoming message in an immutable mapping table (swapped like the chain) and writes to the registry. Preset actions go into a FIFO for the message thread.
- `MacroBank`: 8 macros, each mapped to N targets with min, max and curve. When a macro moves, it writes its targets through the registry.

## Effects

- Every effect implements `IEffect` (see `Source/Effects/IEffect.h`).
- pump effects are wrapped by thin adapters in `Source/Effects/adapters/`. The adapter calls pump's DSP classes from the `external/pump` submodule. pump code is never copied or edited.
- `EffectRegistry` creates effects by string id (`pump.spring`, `util.gain`, …).

## Custom pedals (Phase 6)

- A custom pedal is an `IEffect` whose inside is a graph: nodes (any registered effect, Split, Mix, Gain, Filter, LFO, EnvFollower, FeedbackSend/Return, In, Out) and wires.
- The graph is compiled on the message thread into a flat `PedalRenderPlan` (an ordered list of node runs and buffer copies), then swapped in atomically.
- Cycles are allowed only through a FeedbackSend/Return pair, which adds one block of delay. Any other cycle fails to compile.

## Files

- Board: `*.board` JSON (`scaffold/schemas/board.schema.json`), with `schemaVersion`.
- Pedal: `*.pedal` JSON (`scaffold/schemas/pedal.schema.json`).
- An unknown effect id loads as an empty slot plus a warning. Never crash on a bad file.

## Directory map

```
Source/App        Main.cpp, MainWindow
Source/Common     AllocationGuard (debug-only audio-thread allocation detector), Ids
Source/Engine     AudioEngine, BoardProcessor, ChainSnapshot, Slot, BoardModel
Source/Effects    IEffect, EffectRegistry, adapters/
Source/Control    ParameterRegistry, MidiRouter, KeyRouter, MacroBank
Source/Modular    PedalGraph, PedalRenderPlan, PedalEffect, nodes/
Source/Presets    BoardSerializer, PedalSerializer
Source/UI         BoardView, SlotView, SlotEditor, PatcherView, MacroStrip, SettingsView
Tests             one test file per class under test
```
