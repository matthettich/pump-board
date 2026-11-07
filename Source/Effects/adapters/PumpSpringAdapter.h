#pragma once
// Card 1.3: pump SPRING behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's SpringParams.hpp exactly (the map in
// docs/PUMP_DSP_MAP.md is the reference). pump's own `bypass` param is not duplicated: the slot's
// on/off switch is the board's bypass.
#include "Effects/IEffect.h"
#include "SpringEngine.hpp"
#include <vector>

namespace pb
{
class PumpSpringAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.spring"; }
    juce::String displayName() const override { return "pump SPRING"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }          // pump SPRING reports no latency
    double tailSeconds() const noexcept override { return settings.decay * 2.0; }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::spring::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kModel = 0, kDwell, kDecay, kTension, kDrip, kTone, kLowCut, kPreDelay, kWobble, kWidth, kGrit,
        kMix, kOutGain, kNumParams
    };

    pump::spring::Engine engine;
    pump::spring::Settings settings;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
