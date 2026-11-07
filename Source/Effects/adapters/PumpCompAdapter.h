#pragma once
// Card 5.3 (phase 5): pump COMP behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's CompParams.hpp exactly (docs/PUMP_DSP_MAP.md is the
// reference). pump's own `bypass` param is not duplicated: the slot's on/off switch is the board's bypass.
#include "Effects/IEffect.h"
#include "CompEngine.hpp"
#include <vector>

namespace pb
{
class PumpCompAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.comp"; }
    juce::String displayName() const override { return "pump COMP"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // Zero with pump's defaults. Lookahead and oversampling both add latency and both are params here, so this
    // follows them — the chain does not compensate for it (see NOTES.md, cards 5.1-5.3).
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.05; }      // lookahead and the oversampler, nothing more

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::comp::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kStyle = 0, kThreshold, kRatio, kKnee, kRange, kAttack, kRelease, kAutoRelease, kHold, kLookahead,
        kMix, kInGain, kOutGain, kAutoMakeup, kLink, kChannelMode, kScSource, kScGain, kScHpf, kScLpf,
        kScListen, kOversampling, kNumParams
    };

    pump::comp::Engine engine;
    pump::comp::Settings settings;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
