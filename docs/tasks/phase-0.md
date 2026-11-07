# Phase 0 — Skeleton and passthrough

Goal: an app that opens, picks an audio device, passes audio straight through with meters and a latency readout. No effects yet.

---

## Card 0.1 — Project builds

**Files:** `CMakeLists.txt` (from scaffold), `Source/App/Main.cpp`, `Tests/TestMain.cpp`, stub `.cpp` files for every scaffold header so the library links.
**Give the model:** ARCHITECTURE.md, CMakeLists.txt, every scaffold header.

Steps:
1. `Main.cpp`: a `juce::JUCEApplication` that opens an empty `DocumentWindow` titled "pump BOARD", 1100×700, resizable.
2. For every scaffold header, add a `.cpp` with empty bodies (return defaults) so it compiles. Mark each body `// TODO card X.Y` with the card that will fill it.
3. `Tests/TestMain.cpp`: runs `juce::UnitTestRunner` over all tests, prints results, returns non-zero on any failure.
4. One trivial test (`SmokeTest`) that passes.

Checks:
- `cmake -B build -G Xcode && cmake --build build --config Debug` succeeds with no errors.
- The app opens a window. The test binary prints 1 passed.
- Any fix to a scaffold header (a missing include, a typo) is logged in NOTES.md.

---

## Card 0.2 — AudioEngine and device settings

**Files:** `Source/Engine/AudioEngine.h/.cpp`, `Source/UI/SettingsView.h/.cpp`, `Source/App/MainWindow.h/.cpp`.

Steps:
1. `AudioEngine` owns a `juce::AudioDeviceManager` and a `juce::AudioProcessorPlayer`. `initialise()` opens 2 in / 2 out at 48 kHz with a 64-sample buffer, falling back to 128 if 64 fails.
2. It saves and restores the device setup XML in the app's user data folder (`~/Library/Application Support/pump BOARD/audio.xml`).
3. `SettingsView` wraps `juce::AudioDeviceSelectorComponent` (inputs 1–2, outputs 2, no MIDI out). It also shows a MIDI input enable list.
4. `MainWindow` opens SettingsView from a "Settings" button.

Checks:
- Choosing an interface, restarting the app, and seeing it remembered.
- Switching the buffer size between 64 and 128 doesn't crash.

---

## Card 0.3 — Passthrough BoardProcessor with meters

**Files:** `Source/Engine/BoardProcessor.cpp`, `Source/Control/ParameterRegistry.cpp`, `Source/Control/MidiRouter.cpp` (stub `handle` that does nothing), `Source/UI/MeterBar.h/.cpp`, `Tests/ParameterRegistryTests.cpp`.

Steps:
1. Implement `ParameterRegistry` fully: add / find / remove / get / set / normalised / toggle / changeCounter. Clamp on set.
2. `BoardProcessor` registers `board/bypass` (toggle, default 0), `board/inputTrim` and `board/outputTrim` (−24..+12 dB, default 0).
3. `processBlock` with an empty chain: input trim → output trim → hard safety limiter (clip at −0.3 dBFS for now). If the input is mono (input channel 2 silent for 1 s, or a setting), copy L to R.
4. Update `inPeak` / `outPeak` with the block's max absolute sample, decayed on the message side.
5. `MeterBar` draws a vertical meter from those atomics on a 30 Hz timer. Show in and out meters plus two trim sliders bound to the registry.

Tests (`ParameterRegistryTests`): add returns the same index for the same id; set clamps; normalised round-trips within 1e-6; toggle flips 0/1; remove frees the index for reuse.

Checks:
- Tests pass.
- Playing an instrument through the app is audible, and the meters move. 10 minutes at 64 samples with no dropouts.

---

## Card 0.4 — Latency readout

**Files:** `Source/Engine/LatencyProbe.h/.cpp`, `Source/UI/SettingsView.cpp`.

Steps:
1. Show the device's reported latency: `(inputLatency + outputLatency + bufferSize) / sampleRate`, in ms, in the settings view and in the main window's status bar.
2. Add a "Measure round trip" button. While measuring, BoardProcessor (via a flag) outputs a single click on both channels and records input for 500 ms, then finds the click by peak. Show the measured result in ms. The user patches an output to an input cable for this; say so in the button's tooltip.
3. The measurement code allocates its record buffer before starting, never in the audio callback.

Checks:
- The reported latency is shown and changes when the buffer size changes.
- With a loopback cable, the measured round trip is within 1 ms across three runs.
