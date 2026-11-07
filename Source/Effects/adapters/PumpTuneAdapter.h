#pragma once
// pump TUNE behind IEffect: pitch correction, harmonies and formants for a solo voice (PSOLA resynthesis on a YIN
// pitch tracker). pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units match
// pump's TuneParams.hpp, and the mapping to `pump::tune::Settings` is pump's own `PluginTune::settings()`.
//
// WHAT A PEDAL CAN DO WITHOUT MIDI AND WITHOUT A WINDOW — and what is deliberately left out:
//   - MODE (Live / Graph) is fixed to Live. Graph replays notes captured and edited on TUNE's note-graph in its own
//     window (`setNotes`, `setTimeline`, `setCapture`); a slot has no GUI note editor and no host timeline.
//   - MIDI (Off / Lead notes / Harmony notes) is fixed to Off. A pedal has no MIDI input into the audio path, so the
//     note to sing is always the nearest one in KEY / SCALE, and harmonies are SCALE steps or FIXED semitones (the
//     "held MIDI notes" harmony mode is not offered either).
//   - RANGE (High / Mid / Low) is fixed to Mid. A range change makes the engine rebuild its pitch tracker and PSOLA
//     buffers inside `setSettings()` (it allocates), which an audio-thread setParam path must never do. It also
//     moves the latency (17-27 ms). Mid (70-900 Hz) is pump's default.
//   - pump's own `bypass` is not duplicated: the slot's on/off switch is the board's bypass.
// Exposed: key, scale, retune speed, amount, humanize, flex, vibrato, transpose, formant (+ follow), the four
// harmonies (mode, interval, level, pan), harmony formant/humanize, lead and harmony levels, output gain.
#include "Effects/IEffect.h"
#include "TuneEngine.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace pb
{
class PumpTuneAdapter final : public IEffect
{
public:
    PumpTuneAdapter();

    juce::String typeId() const override { return "pump.tune"; }
    juce::String displayName() const override { return "pump TUNE"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The PSOLA's own delay: 1.25 x the longest period plus 64 samples (about 19 ms at 48 kHz for RANGE Mid). TUNE
    // has no dry path, so the whole signal is that late. It is fixed once prepared (RANGE is not exposed).
    int latencySamples() const noexcept override { return prepared ? engine->latency() : 0; }
    double tailSeconds() const noexcept override { return 0.05; }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::tune::Settings& currentSettings() const noexcept { return settings; }

    // How many times the engine was handed a settings struct since construction; a steady knob must not move it.
    int settingsPushCount() const noexcept { return pushes; }

private:
    enum ParamIndex
    {
        kKey = 0, kScale, kRetune, kAmount, kHumanize, kFlex, kVibDepth, kVibRate, kTranspose,
        kFormant, kFormantFollow, kHarmMode,
        kInterval0, kInterval1, kInterval2, kInterval3,
        kLevel0, kLevel1, kLevel2, kLevel3,
        kPan0, kPan1, kPan2, kPan3,
        kHarmFormant, kHarmHumanize, kLead, kHarmLevel, kOutGain,
        kNumParams
    };

    // A push that changes nothing must not touch the engine (Slot::process re-pushes every parameter every block).
    void assignInt (int& field, int value) noexcept
    {
        if (field == value)
            return;

        field = value;
        dirty = true;
    }

    void assignDouble (double& field, float value) noexcept
    {
        if (std::abs (field - (double) value) <= 1.0e-6)
            return;

        field = (double) value;
        dirty = true;
    }

    std::unique_ptr<pump::tune::Engine> engine;   // on the heap: the registry builds throwaway instances for typeId()
    pump::tune::Settings settings;                // mode Live, midi Off, range Mid: see the header comment
    bool dirty = true;
    bool prepared = false;
    int pushes = 0;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;              // a single-channel host: the engine wants two inputs
};
} // namespace pb
