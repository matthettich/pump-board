#pragma once
// Card 5.16 (phase 5): pump STUTTER behind IEffect — tempo-locked stutters and tape stops, auto or triggered, with
// pitch, decay, filter and variation per repeat.
// pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units match
// pump's StutterParams.hpp. pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "StutterEngine.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pb
{
class PumpStutterAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.stutter"; }
    juce::String displayName() const override { return "pump STUTTER"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }     // pump STUTTER reports none
    double tailSeconds() const noexcept override { return 0.1; }

    const pump::stutter::Settings& currentSettings() const noexcept { return settings; }
    const pump::Transport& currentTransport() const noexcept { return transport; }

    double tempo() const noexcept { return bpm; }
    void setTempo (double newBpm) noexcept override;

private:
    enum ParamIndex
    {
        kTrigger = 0, kInterval, kChance, kLength, kGrid, kGate, kPitch, kDecay, kFilter, kVariation,
        kTape, kMix, kOutGain, kPatternOn, kPattern, kPatternStep,
        kNumParams
    };

    pump::stutter::Engine engine;
    pump::stutter::Settings settings;
    pump::Transport transport;
    double bpm = 120.0;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
