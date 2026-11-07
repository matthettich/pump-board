#pragma once
// Card 5.12 (phase 5): pump STRIP behind IEffect — a console channel strip: eight models of input colour, filters,
// four bands of EQ, a gate and a compressor, with an order switch and oversampling. pump's engine is used as-is;
// this file only translates. Param ids, ranges, defaults and units match pump's StripParams.hpp.
// pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
#include "Effects/IEffect.h"
#include "StripEngine.hpp"
#include <vector>

namespace pb
{
class PumpStripAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.strip"; }
    juce::String displayName() const override { return "pump STRIP"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The oversampler's latency, at pump's default 2x. Changing OVERSAMPLING moves it mid-stream, like HEAT and LIMIT.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const pump::strip::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kModel = 0, kInput, kColor, kPhase, kHpf, kLpf, kEqOn,
        kLfGain, kLfFreq, kLfBell,
        kLmfGain, kLmfFreq, kLmfQ,
        kHmfGain, kHmfFreq, kHmfQ,
        kHfGain, kHfFreq, kHfBell,
        kGateOn, kGateThresh, kGateRange, kGateRelease, kGateMode,
        kCompOn, kCompThresh, kCompRatio, kCompAttack, kCompRelease, kCompAuto, kCompMakeup, kCompMix, kScHpf,
        kOrder, kOutGain, kAutoGain, kOversampling,
        kNumParams
    };

    pump::strip::Engine engine;
    pump::strip::Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
