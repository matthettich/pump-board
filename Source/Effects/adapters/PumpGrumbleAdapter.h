#pragma once
// pump GRUMBLE behind IEffect: a creature voice. The input (the mono sum) feeds four layers — BEAST (pitch/formant
// resynthesis + fry), SPLIT (a second voice with wobble), GROWL (a pulse-train throat through a 20-band vocoder) and
// SWARM (a granular cloud) — blended on a two-axis MORPH pad, then GATE, DRIVE, TONE, CAVE and MIX. pump's engine is
// used as-is; param ids, ranges, defaults and units match pump's GrumbleParams.hpp, and the mapping to
// `pump::grumble::Settings` is pump's own `PluginGrumble::sync()`. pump's `bypass` is not duplicated (the slot's
// switch is the board's bypass).
//
// The morph pad is two parameters, `morph_x` and `morph_y` (0..100, the pad's centre is 50/50): the corners are the
// layers (BEAST top left, SPLIT top right, GROWL bottom left, SWARM bottom right), exactly as in pump's window.
// Nothing here needs MIDI, a sidechain or a GUI, so nothing is left out.
#include "Effects/IEffect.h"
#include "GrumbleEngine.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace pb
{
class PumpGrumbleAdapter final : public IEffect
{
public:
    PumpGrumbleAdapter();

    juce::String typeId() const override { return "pump.grumble"; }
    juce::String displayName() const override { return "pump GRUMBLE"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The resynthesis's delay (1.25 x the longest period of the 55 Hz floor, plus 64 samples: about 24 ms at
    // 48 kHz), whatever layers are on. MIX's dry path is delayed by the same amount, so the whole pedal is that late.
    int latencySamples() const noexcept override { return prepared ? engine->latency() : 0; }
    double tailSeconds() const noexcept override { return 1.5; }      // the swarm line is 1.2 s long, the cave is short

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::grumble::Settings& currentSettings() const noexcept { return settings; }

    // How many times the engine was handed a settings struct since construction; a steady knob must not move it.
    int settingsPushCount() const noexcept { return pushes; }

private:
    enum ParamIndex
    {
        kBeastOn = 0, kBeastPitch, kBeastFormant, kBeastFry, kBeastLevel, kBeastPan,
        kSplitOn, kSplitPitch, kSplitFormant, kSplitWobble, kSplitLevel, kSplitPan,
        kGrowlOn, kGrowlPitch, kGrowlRough, kGrowlBreath, kGrowlSize, kGrowlLevel, kGrowlPan,
        kSwarmOn, kSwarmPitch, kSwarmSpread, kSwarmDensity, kSwarmGrain, kSwarmLevel, kSwarmWidth,
        kMorphX, kMorphY, kGate, kDrive, kTone, kCave, kMix, kOutGain,
        kNumParams
    };

    void assignFloat (float& field, float value) noexcept
    {
        if (std::abs (field - value) <= 1.0e-6f)
            return;

        field = value;
        dirty = true;
    }

    void assignBool (bool& field, float value) noexcept
    {
        const bool on = value >= 0.5f;

        if (field == on)
            return;

        field = on;
        dirty = true;
    }

    std::unique_ptr<pump::grumble::Engine> engine;   // on the heap: the registry builds throwaway instances
    pump::grumble::Settings settings;
    bool dirty = true;
    bool prepared = false;
    int pushes = 0;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
