#pragma once
// Card 5.9 (phase 5): pump VERB behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's VerbParams.hpp (docs/PUMP_DSP_MAP.md is the reference).
// pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "VerbEngine.hpp"
#include <vector>

namespace pb
{
class PumpVerbAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.verb"; }
    juce::String displayName() const override { return "pump VERB"; }
    std::vector<ParamInfo> params() const override;

    // pump VERB takes the sample rate only; its own buffers are fixed-size and it reports no latency.
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }
    double tailSeconds() const noexcept override { return juce::jlimit (0.0, 10.0, settings.decay); }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::verb::Settings& currentSettings() const noexcept { return settings; }

    // Pre-delay sync needs a tempo. The board has no host, so this stays at pump's own default until the app
    // grows a tempo source; SETTINGS' round-trip tempo is a separate mechanism (card 5.4).
    double tempo() const noexcept { return bpm; }
    void setTempo (double newBpm) noexcept override;

private:
    enum ParamIndex
    {
        kMode = 0, kPredelay, kSync, kNote, kSize, kDecay, kDiffusion, kBalance, kBassMult, kCrossover,
        kHighDamp, kDampFreq, kLowCut, kHighCut, kModDepth, kModRate, kWidth, kAge, kEnvTime, kPitch,
        kShimmer, kDuck, kFreeze, kMix, kOutGain,
        kNumParams
    };

    pump::verb::Engine engine;
    pump::verb::Settings settings;
    double bpm = 120.0;
    bool dirty = true;          // pump applies settings on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
