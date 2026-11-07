# pump BOARD

A Mac-only JUCE standalone guitar pedalboard: **16 slots in series**, **24 pedals** built from
[pump](https://github.com/matthettich/pump)'s DSP engines, **8 macro knobs**, MIDI and computer-key mapping,
presets with program changes, and a device-wide tempo you can type, drag, or drive with a MIDI clock.

Playable, but unfinished. Phases 0-4 of the plan are done and phase 5 is in progress. The DSP adapters are
verified against pump's own engines (null tests, bit-identical); what nobody has done yet is judge how it
sounds. `docs/HANDOVER.md` keeps that list honest.

## Pedals

SPRING, ECHO, TUBE, CRUSH, SWIRL, COMP, HEAT, TONE, VERB, LIMIT, SNAP, STRIP, CALM, GATE, GRAIN, STUTTER, CHOP,
WIDE, BAND, EQ, TWANG, TUNE, VOX, GRUMBLE.

All twenty-four are pump's DSP compiled in as source, unmodified. The adapter for each one only translates
parameters and buffers; `external/pump` is never edited (a house rule, and the reason TWANG drives COIL's
engine rather than having one of its own).

## Requirements

- macOS 14 or newer on Apple silicon (the CMake file pins `arm64`)
- Xcode command line tools
- CMake 3.24+ and Ninja
- JUCE 9.0.5 and pump: both are git submodules, pinned to the commits this app is built against

## Build

```bash
git clone --recurse-submodules https://github.com/matthettich/pump-board.git
cd pump-board
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_SYSROOT="$SDKROOT"
cmake --build build
```

If you already cloned without submodules: `git submodule update --init --recursive`.

`SDKROOT` is spelled out because this machine's toolchain does not find the sysroot on its own; drop the export
and `-DCMAKE_OSX_SYSROOT` on a machine where the default works.

Debug is worth keeping: `PB_ALLOCATION_GUARD` is a Debug-only feature, and it is the proof that the audio
callback never allocates, locks, or does I/O.

## Download a build

Every push to `main` builds the app on GitHub's Apple-silicon Macs and publishes it as the **latest** release:
<https://github.com/matthettich/pump-board/releases/tag/latest> (`pump-BOARD-mac-arm64.zip`). Unzip it and drag
`pump BOARD.app` anywhere. It is not signed, so the first launch needs **right-click → Open → Open** (or
`xattr -dr com.apple.quarantine "pump BOARD.app"`). The workflow is `.github/workflows/mac-build.yml`.

## Run

```bash
open "build/PumpBoard_artefacts/Debug/pump BOARD.app"
```

## Tests

```bash
./build/PumpBoardTests_artefacts/Debug/PumpBoardTests     # 3204 expectations, 0 failing
```

The suite covers the engine, the router, macros, presets, every adapter's null test against pump's engine, and
the allocation guard. A plain configure (no `-DCMAKE_BUILD_TYPE=Debug`) runs fewer: `AllocationGuardTests` and the new pedals' allocation checks
are `#ifdef PB_ALLOCATION_GUARD`, so those expectations only exist in Debug. Both configurations should
build with **zero warnings** in `Source/` and `Tests/`; pump's and JUCE's headers warn and are left alone.

## Offscreen renders

For looking at the UI with no audio device and no hands:

```bash
BIN="build/PumpBoard_artefacts/Debug/pump BOARD.app/Contents/MacOS/pump BOARD"
"$BIN" --render board.png 2.0                        # the painted design mockup
"$BIN" --render-slots slots.png 2.0                  # the real sixteen slots
"$BIN" --render-editor editor.png 2.0 pump.tube      # one real pedal and its knob panel
```

## Your data

Everything lives in `~/Music/pump BOARD/`: `current.board` (the working copy, reopened on launch),
`Boards/<name>.board` (the boards you saved), and `settings` (device-wide tempo and audio choice). A legacy
`board.xml` is read once for migration and never written again. Nothing is written anywhere else.

## Docs

| file | what it is |
| --- | --- |
| `docs/HANDOVER.md` | state of play: what works, what is untested, open decisions, landmines |
| `docs/NOTES.md` | the decision log, one card per change, newest at the bottom |
| `docs/ARCHITECTURE.md` | how the app is put together |
| `docs/PUMP_DSP_MAP.md` | every pump effect, and what adapting it takes |
| `docs/REVIEW_TEMPLATE.md` | the hand-test checklist for the owner |
| `docs/PROMPT_SYSTEM.md` | the working conventions used to build this |
| `docs/tasks/phase-*.md` | the card-by-card plan, phases 0 through 7 |

## Licence

No licence has been chosen for this repository yet, so it is not offered for reuse. Third-party code keeps its
own terms: JUCE (`external/JUCE/LICENSE.md`), pump (`external/pump/LICENSE`), and the three Bitstream Vera /
Share Tech Mono faces compiled into the app (`SIL OFL`, licences shipped in the app bundle's `Resources`).
