#pragma once
// Card 5.15 (phase 5): pump GRAIN behind IEffect — a granular processor: grain size, density, spray, tempo sync,
// pitch and pitch spread, scales, a filter, feedback, space, motion and a morph between smooth and glitchy.
// pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units match
// pump's GrainParams.hpp. pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "GrainEngine.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pb
{
class PumpGrainAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.grain"; }
    juce::String displayName() const override { return "pump GRAIN"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }     // pump GRAIN reports none
    double tailSeconds() const noexcept override { return 0.2; }   // grains in flight

    const pump::grain::Settings& currentSettings() const noexcept { return settings; }
    const pump::Transport& currentTransport() const noexcept { return transport; }

    double tempo() const noexcept { return bpm; }
    void setTempo (double newBpm) noexcept override;

private:
    enum ParamIndex
    {
        kPosition = 0, kSize, kDensity, kSpray, kSync, kDivision, kEnvelope, kReverse, kStereo, kPitch,
        kPitchSpread, kQuantize, kFeedback, kFilter, kCutoff, kResonance, kSpace, kMotionRate, kMotionDepth,
        kMorphX, kMorphY, kFreeze, kMix, kOutGain,
        kNumParams
    };

    pump::grain::Engine engine;
    pump::grain::Settings settings;
    pump::Transport transport;
    double bpm = 120.0;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
