# pump DSP map

**Card 1.1 — investigation.** Does pump's DSP run without its plugin wrappers?

**Verdict: yes, uniformly.** Every effect in `external/pump` is built as a framework-free engine header
(`XxxEngine.hpp`) beside a thin DPF wrapper (`PluginXxx.cpp`, `UIXxx.cpp`). The engine headers include only pump's
own `common/dsp/*` and their own `XxxParams.hpp` — no DISTRHO, no `AudioProcessor`, no editor, no JUCE. The same
`Settings` struct is shared by the DSP and the UI, which is why the parameter tables below are the single source of
truth for both.

- repo: `https://github.com/matthettich/pump`, cloned from that URL
- pump commit: `7d981aae180ff32a272ea19b09db7707b4a77006` (main, 2026-11-05) — verified level with upstream main
- Read-only: `external/pump`, never edited. Adapters wrap; they do not fork.
- **Licence:** pump's own code is "Copyright (c) 2026 wake. All rights reserved" — visible, not licensed for
  redistribution by anyone else. Compiling it into pump BOARD is fine for the author and means the board cannot be
  licensed permissively while it contains pump code. One exception: `plugins/machine/` (pump MACHINE) is GPL-3.0-only,
  together with the pump code compiled into it — **nothing from `plugins/machine/` may enter this app**, or the whole
  board inherits GPL-3.0. It is a drum machine, not an effect, so it is not on this map.
- Third-party reachable from our include path: the fonts (`common/fonts`) are SIL OFL 1.1 — the OFL texts must ship
  with the app once fonts are embedded. `pffft` is only needed by FFT-based effects (`dsp/FFT.hpp`, `Room.hpp`), none
  of which the three Phase 1 engines pull in.
- Excluded from the map by the plan: instruments, ROOM, CONVERT, AMP.

## The engine API (identical across the three Phase 1 effects)

```cpp
namespace pump::<effect> {
    struct Settings { /* doubles, in the plugin's own units: dB, %, ms, Hz */ };
    class Engine {
        void prepare (double sampleRate, int maxBlock);
        void setSettings (const Settings&);      // audio thread, at block start
        void process (float* L, float* R, int n); // in place, stereo
        int  latency() const;
    };
}
```

Notes that matter for `IEffect`:

- **Values arrive in plugin units, not 0..1.** `Settings::mix` is 0..100, `drive` is dB, `decay` is seconds. The
  adapter's `setParam` must convert from `ParamInfo`'s range, and `ParamRegistry` stores those same units.
- **`setSettings` once per block is the intended use.** The engines smooth internally (20 ms coefs, control ticks
  every 16 samples), so the adapter must not call it per sample.
- **Latency is per effect and can change at runtime.** SPRING and ECHO report 0; TUBE reports its oversampler's
  latency, which changes with the OVERSAMPLING setting (`latency()` reads `curLg`). In a 16-slot series chain this
  means the board needs a latency-compensation plan (or to forbid mid-play oversampling changes), not just a sum.
- **Some effects want tempo.** ECHO's `setTempo(double bpm)` drives synced times; the board has no host, so the
  engine needs the app's own tempo (later card).

## Every effect

`separable?` is judged from the engine header's includes and entry points, not by trying to link yet. Only the three
Phase 1 effects are compiled into our build; CRUSH, SWIRL, COMP, HEAT, TONE, VERB, LIMIT, SNAP, STRIP, CALM, GATE, GRAIN, STUTTER, CHOP, WIDE, BAND and EQ joined them in
phase 5 (cards 5.1-5.24). pffft is linked, which unblocks every FFT-based pedal still on this map. The rest are
mapped. WOB looks like an effect on the list but is an instrument (it needs MIDI note events), so it is not one.

| effect | engine header | params header | separable? | notes |
| --- | --- | --- | --- | --- |
| EQ | `plugins/eq/EQEngine.hpp` + `EQDsp.hpp` | `EQParams.hpp` | **yes — compiled** | 24 bands, dynamic bands; the adapter exposes 8 (card 5.20), and only its zero-latency mode is null-tested |
| COMP | `CompEngine.hpp` + `CompDsp.hpp` | `CompParams.hpp` | **yes — compiled** | external sidechain has no bus in a slot; lookahead and oversampling add latency |
| LIMIT | `LimitEngine.hpp` + `LimitDsp.hpp` | `LimitParams.hpp` | **yes — compiled** | lookahead + oversampling latency; both reconfigure mid-stream (card 5.10) |
| CALM | `CalmEngine.hpp` + `CalmDsp.hpp` | `CalmParams.hpp` | **yes — compiled** | FFT latency; the 48 draggable nodes are not exposed (globals only) |
| TONE | `ToneEngine.hpp` | `ToneParams.hpp` | **yes — compiled** | STFT EQ: latency = FFT size (2048 or 4096); `quality` switches mid-stream |
| BAND | `BandEngine.hpp` + `BandDsp.hpp` | `BandParams.hpp` | **yes — compiled** | multiband; pump has no per-band parameters, so the adapter's 20 per-band controls are its own (card 5.19) |
| WIDE | `WideEngine.hpp` + `WideDsp.hpp` | `WideParams.hpp` | **yes — compiled** | per-band width and stereoize plus mono bass (card 5.18) |
| HEAT | `HeatEngine.hpp` | `HeatParams.hpp` | **yes — compiled** | brings `pffft.c` into our build (linear-phase splitter); defaults to 4x, so it reports latency |
| METER | `MeterEngine.hpp` + `MeterDsp.hpp` | `MeterParams.hpp` | yes | **not added at the owner's request** — metering only, read-only output params, no audio change |
| MASTER | `MasterEngine.hpp` | `MasterParams.hpp` | yes | **not added at the owner's request**; an adapter was written and withdrawn |
| TUBE | `TubeEngine.hpp` | `TubeParams.hpp` | **yes — compiled** | oversampling latency, see above |
| SNAP | `SnapEngine.hpp` | `SnapParams.hpp` | **yes — compiled** | transient designer; lookahead latency, off by default, applied behind a fade |
| GATE | `GateEngine.hpp` | `GateParams.hpp` | **yes — compiled** | step gate; needs a tempo (adapter runs 120 BPM); the 32-step pattern is not exposed |
| ECHO | `EchoEngine.hpp` + `EchoDsp.hpp` | `EchoParams.hpp` | **yes — compiled** | needs `setTempo` |
| SPRING | `SpringEngine.hpp` | `SpringParams.hpp` | **yes — compiled** | also exposes `kick()`, scope rings and meters |
| VERB | `VerbEngine.hpp` + `VerbDsp.hpp` | `VerbParams.hpp` | **yes — compiled** | 12 modes, zero latency; pre-delay sync needs a tempo (fixed 120 BPM for now) |
| GRAIN | `GrainEngine.hpp` | `GrainParams.hpp` | **yes — compiled** | granular, zero latency; tempo sync free-runs at 120 BPM |
| VOX | `VoxEngine.hpp` + `VoxFx.hpp` | `VoxParams.hpp` | yes | needs a carrier / MIDI sidechain (not in v1) |
| TUNE | `TuneEngine.hpp` | `TuneParams.hpp` | yes | MIDI note input (not in v1) |
| CRUSH | `CrushEngine.hpp` | `CrushParams.hpp` | **yes — compiled** | 6 ms media transport: real latency, reported but not compensated |
| SWIRL | `SwirlEngine.hpp` | `SwirlParams.hpp` | **yes — compiled** | zero latency except 3 ms in the flanger's THRU-0; takes a `pump::Transport` |
| STUTTER | `StutterEngine.hpp` | `StutterParams.hpp` | **yes — compiled** | needs tempo (120 BPM); pump's eight move toggles are not parameters |
| CHOP | `ChopEngine.hpp` | `ChopParams.hpp` | **yes — compiled** | needs tempo (adapter runs 120 BPM); records the last bar (card 5.17) |
| TWANG | — (uses `plugins/coil/`'s engine) | `TwangParams.hpp` | **yes — compiled** | drives `pump::coil::Engine` in resonator mode, which excites from the audio input and needs no MIDI (card 5.21); COIL's DSP is compiled in, not linked |
| STRIP | `StripEngine.hpp` | `StripParams.hpp` | **yes — compiled** | 8 console models, 4-band EQ, gate, comp; oversampler latency (63 samples even at 1x) |

## Phase 1 detail

### pump SPRING — `pump::spring::Engine`

Settings: `model, dwell, decay, tension, drip, tone, lowCut, preDelay, wobble, width, grit, mix, outDb`.

| id | name | range | default | unit | scale |
| --- | --- | --- | --- | --- | --- |
| `model` | Model | 0..5 | 0 (Two-spring) | — | int, 6 names |
| `dwell` | Dwell | 0..100 | 50 | % | linear |
| `decay` | Decay | 0.3..8 | 2.5 | s | log |
| `tension` | Tension | -100..100 | 0 | % | linear |
| `drip` | Drip | 0..100 | 50 | % | linear |
| `tone` | Tone | -100..100 | 0 | % | linear |
| `low_cut` | Low Cut | 20..1000 | 100 | Hz | log (20 = off) |
| `pre_delay` | Pre-Delay | 0..250 | 0 | ms | power |
| `wobble` | Wobble | 0..100 | 15 | % | linear |
| `width` | Width | 0..100 | 70 | % | linear |
| `grit` | Grit | 0..100 | 35 | % | linear |
| `mix` | Mix | 0..100 | 30 | % | linear |
| `out_gain` | Output Gain | -24..24 | 0 | dB | linear |
| `bypass` | Bypass | 0..1 | 0 | — | bool |

Extra API: `kick(double strength = 1.0)` (the KICK key), `visRate()`, `visF[] / visB[]` scope rings, `kickAge`.

### pump ECHO — `pump::echo::Engine`

Settings: `mode, timeL, timeR, sync, noteL, noteR, link, feedback, stereo, width, modDepth, modRate, age, drive,
lowCut, highCut, diffuse, duck, freeze, pitch, fine, pitchMode, mix, outDb`.

| id | name | range | default | unit | scale |
| --- | --- | --- | --- | --- | --- |
| `mode` | Mode | 0..4 | 0 (Digital) | — | int, 5 names |
| `time_l` | Time L | 1..4000 | 500 | ms | log |
| `time_r` | Time R | 1..4000 | 750 | ms | log |
| `sync` | Sync | 0..1 | 1 | — | bool |
| `note_l` | Note L | 0..N | 1/4 | — | int, note names |
| `note_r` | Note R | 0..N | 1/4D | — | int, note names |
| `link` | Link | 0..1 | 0 | — | bool |
| `feedback` | Feedback | 0..120 | 35 | % | linear |
| `stereo` | Stereo Mode | 0..2 | 0 (Stereo) | — | int, 3 names |
| `width` | Width | 0..100 | 100 | % | linear |
| `mod_depth` | Mod Depth | 0..100 | 0 | % | linear |
| `mod_rate` | Mod Rate | 0.05..10 | 0.8 | Hz | log |
| `age` | Age | 0..100 | 0 | % | linear |
| `drive` | Drive | 0..24 | 0 | dB | linear |
| `low_cut` | Low Cut | 10..2000 | 10 | Hz | log (10 = off) |
| `high_cut` | High Cut | 1000..22000 | 22000 | Hz | log (22000 = off) |
| `diffuse` | Diffuse | 0..100 | 0 | % | linear |
| `duck` | Duck | 0..100 | 0 | % | linear |
| `freeze` | Freeze | 0..1 | 0 | — | bool |
| `pitch` | Pitch | -24..24 | 12 | st | int |
| `fine` | Fine | -100..100 | 0 | — | linear |
| `pitch_mode` | Pitch Mode | 0..1 | 0 (Once) | — | int, 2 names |
| `mix` | Mix | 0..100 | 30 | % | linear |
| `out_gain` | Output Gain | -24..24 | 0 | dB | linear |
| `bypass` | Bypass | 0..1 | 0 | — | bool |

Extra API: `setTempo(double bpm)`.

### pump TUBE — `pump::tube::Engine`

Settings: `mode, drive, bias, sag, voltage, stages, focus, lowCut, tone, highCut, mix, outDb, autoGain, oversampling`.

| id | name | range | default | unit | scale |
| --- | --- | --- | --- | --- | --- |
| `mode` | Circuit | 0..5 | 1 (Triode) | — | int, 6 names |
| `drive` | Drive | 0..36 | 12 | dB | linear |
| `bias` | Bias | -100..100 | 0 | % | linear |
| `sag` | Sag | 0..100 | 25 | % | linear |
| `voltage` | Voltage | 0..100 | 100 | % | linear |
| `stages` | Stages | 1..3 | 1 | — | int (not automatable) |
| `focus` | Focus | 0..3 | 0 (Full) | — | int, 4 names |
| `low_cut` | Low Cut | 10..1000 | 10 | Hz | log |
| `tone` | Tone | -100..100 | 0 | % | linear |
| `high_cut` | High Cut | 1000..22000 | 22000 | Hz | log |
| `mix` | Mix | 0..100 | 100 | % | linear |
| `out_gain` | Output Gain | -24..24 | 0 | dB | linear |
| `auto_gain` | Auto Gain | 0..1 | 1 | — | bool |
| `oversampling` | Oversampling | 0..3 | 2 (4x) | — | int, 1x..8x (not automatable) |
| `bypass` | Bypass | 0..1 | 0 | — | bool |

Extra API: `latency()`, `oversampling()`, plus UI feeds (`inLevelDb`, `sagNow`, `biasShift`, `autoGainDb`).

## Two interface questions for review (not worked around)

1. **`ParamInfo` has no choice labels.** SPRING's `model`, ECHO's `mode`/`stereo`/`note`/`pitch_mode` and TUBE's
   `mode`/`focus`/`oversampling` are integer switches with named positions in pump (`kModelNames`, `kModeNames`, …).
   `pb::ParamInfo` only describes a numeric range, so a generic editor would show "2.00" where pump shows "Short
   tank". Options: add an optional `choices` vector to `ParamInfo`, or let each adapter supply its own editor. This is
   a scaffold header change, so it is written here instead of made.
2. **Scaling.** pump's `PScale::Log` is what `ParamInfo::skewCentre` is for: for a log parameter the knob's midpoint
   value is `sqrt(min * max)`. `PUnit` maps onto `ParamInfo::unit` directly. Follow this in the adapters so the knobs
   feel like pump's.

## Presets

Every `XxxParams.hpp` also carries a `presets()` table (Factory / Sources / Creative / Chains). That is free content
for board presets later — pump's presets are already in plugin units, so they can seed `BoardData` without
conversion.
