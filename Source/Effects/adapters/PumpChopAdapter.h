#pragma once
// Card 5.17 (phase 5): pump CHOP behind IEffect — a tempo-locked bar chopper: it records the last bar and slices it
// into 8 or 16, in half/one/two/four-bar captures, quantised to the grid, with an 8-pad hold system.
// pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units match
// pump's ChopParams.hpp. pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
//
// The eight pads are played from keys in pump; the board has no pad surface for a slot, so the pedal captures and
// slices but nothing holds a pad. docs/NOTES.md records that.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "ChopEngine.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pb
{
class PumpChopAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.chop"; }
    juce::String displayName() const override { return "pump CHOP"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const pump::chop::Settings& currentSettings() const noexcept { return settings; }
    const pump::Transport& currentTransport() const noexcept { return transport; }

    double tempo() const noexcept { return settings.bpm; }
    void setTempo (double newBpm) noexcept override;

private:
    enum ParamIndex
    {
        kSlices = 0, kLengthP, kQuantise, kHold, kLoopTwo, kMix, kOutGain,
        kNumParams
    };

    pump::chop::Engine engine;
    pump::chop::Settings settings;
    pump::Transport transport;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
