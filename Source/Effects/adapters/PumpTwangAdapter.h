#pragma once
// Card 5.21 (phase 5): pump TWANG behind IEffect. TWANG is pump's spring resonator and its engine is COIL's
// (`pump::coil::Engine` in `resonator` mode), so this adapter pulls COIL's engine in as source, not as a plugin.
//
// The important finding: TWANG is **not** an instrument that needs MIDI notes. In resonator mode COIL excites its
// springs from the audio coming in (`bodyIn = dry * 0.7 + in * x * 2.0` in COIL's processChunk) and mixes back with
// `mix`, and chord tuning ("the chord tunes; notes don't") means the notes path is optional. So a guitarist's
// playing drives it exactly as pump's own plugin does when nothing is plugged into its keyboard. `kick()` — the
// KICK key in TWANG's own window — is deliberately not exposed: a slot has no key to press.
//
// Param ids, ranges, defaults and units match pump's TwangParams.hpp. `bypass` is not exposed. The mapping from
// parameter to `pump::coil::Settings` is pump's own `PluginTwang::sync()`, field for field, including the scaled
// ones (drive is COIL's dwell, tank/harmonic/spread/wobble/tone/width/mix are percentages of 0..1).
#include "Effects/IEffect.h"
#include "CoilEngine.hpp"
#include <cmath>
#include <vector>

namespace pb
{
class PumpTwangAdapter final : public IEffect
{
public:
    struct Settings
    {
        int model = 0, tuning = pump::coil::kTuneChord, root = 9, chord = pump::coil::kChordFifth, octave = 0;
        float tension = 0.0f, drip = 50.0f, decay = 2.5f, release = 0.6f;
        float harmonic = 40.0f, spread = 10.0f, wobble = 15.0f, tone = 0.0f, grit = 35.0f;
        float drive = 50.0f, tank = 0.0f, width = 70.0f, mix = 50.0f, volumeDb = 0.0f;
    };

    juce::String typeId() const override { return "pump.twang"; }
    juce::String displayName() const override { return "pump TWANG"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The resonator adds no delay of its own (pump's bypass reports 0). The tank is a parallel reverb, not a lookahead.
    int latencySamples() const noexcept override { return 0; }
    double tailSeconds() const noexcept override { return (double) settings.decay; }

    const Settings& currentSettings() const noexcept { return settings; }

    // The one place the settings become engine calls; the null test hands its reference engine this same order.
    template <class EngineType>
    void applySettingsTo (EngineType& target) const
    {
        target.setSettings (makeSettings());
    }

    pump::coil::Settings makeSettings() const
    {
        pump::coil::Settings s;
        s.resonator = true;
        s.model = settings.model;
        s.tension = (double) settings.tension;
        s.drip = (double) settings.drip;
        s.decay = (double) settings.decay;
        s.release = (double) settings.release;
        s.harmonic = (double) settings.harmonic * 0.01;
        s.spread = (double) settings.spread * 0.01;
        s.wobble = (double) settings.wobble * 0.01;
        s.tone = (double) settings.tone * 0.01;
        s.grit = (double) settings.grit;
        s.tuning = settings.tuning;
        s.root = settings.root;
        s.chord = settings.chord;
        s.octave = settings.octave;
        s.dwell = (double) settings.drive;          // TWANG's Drive is COIL's dwell
        s.tank = (double) settings.tank * 0.01;
        s.width = (double) settings.width * 0.01;
        s.mix = (double) settings.mix * 0.01;
        s.volumeDb = (double) settings.volumeDb;

        return s;
    }

private:
    enum ParamIndex
    {
        kModel = 0, kTension, kDrip, kDecay, kRelease, kHarmonic, kSpread, kWobble, kTone, kGrit,
        kTuning, kRoot, kChord, kOctave, kDrive, kTank, kWidth, kMix, kVolume,
        kNumParams
    };

    // A push that changes nothing must not touch the engine: Slot::process re-pushes every parameter every block,
    // and COIL reconfigures its springs on setSettings(). Floats compare with a tolerance, since -Wfloat-equal.
    void assignInt (int& field, int value) noexcept
    {
        if (field == value)
            return;

        field = value;
        dirty = true;
    }

    void assignFloat (float& field, float value) noexcept
    {
        if (std::abs (field - value) <= 1.0e-6f)
            return;

        field = value;
        dirty = true;
    }

    // SPRING MODEL needs a *fresh* design, not a live one: in COIL's `design()` the digital branch returns before
    // snapping its delay and gain to their targets, so switching to a digital spring from a tank leaves the digital
    // combs holding the tank's delays — the loop never rings and the voice goes quiet. A model change therefore
    // re-voices TWANG's chord, which designs each voice fresh (`Voice::start` -> `design(s, true)`).
    void revoice() noexcept
    {
        auto s = makeSettings();

        if (s.tuning != pump::coil::kTuneChord)
            return;                      // notes drive the voices in LATCH/MIDI tuning; a chord start would be noise

        s.tuning = pump::coil::kTuneLatch;
        engine.setSettings (s);          // a tuning change: the old chord is let go
        engine.setSettings (makeSettings());   // and back: the chord starts again, designed fresh
    }

    pump::coil::Engine engine;
    Settings settings;
    int appliedModel = 0;                // the model the engine is actually designed for
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> inLeft, inRight, outLeft, outRight;
};
} // namespace pb
