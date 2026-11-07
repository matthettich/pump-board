#pragma once
// Card 5.11 (phase 5): pump SNAP behind IEffect — a transient designer (attack/sustain shaping, band focus,
// optional clipping). pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units
// match pump's SnapParams.hpp. pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
#include "Effects/IEffect.h"
#include "SnapEngine.hpp"
#include <vector>

namespace pb
{
class PumpSnapAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.snap"; }
    juce::String displayName() const override { return "pump SNAP"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The lookahead, when it is switched on. pump applies it at the next block start, so like HEAT/TONE/LIMIT this
    // can change mid-stream.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const pump::snap::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kAttack = 0, kSustain, kAttackTime, kRelease, kLink, kFocus, kBand, kCrossover, kLookahead,
        kClip, kCeiling, kDelta, kMix, kOutGain,
        kNumParams
    };

    pump::snap::Engine engine;
    pump::snap::Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
