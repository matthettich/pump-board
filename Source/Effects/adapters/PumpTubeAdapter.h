#pragma once
// Card 1.4: pump TUBE behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's TubeParams.hpp exactly (docs/PUMP_DSP_MAP.md is the reference).
// pump's own `bypass` param is not duplicated: the slot's on/off switch is the board's bypass.
#include "Effects/IEffect.h"
#include "TubeEngine.hpp"
#include <vector>

namespace pb
{
class PumpTubeAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.tube"; }
    juce::String displayName() const override { return "pump TUBE"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The oversampler delays the signal, and OVERSAMPLING changes how much: 1x has none, 8x the most.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::tube::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kMode = 0, kDrive, kBias, kSag, kVoltage, kStages, kFocus, kLowCut, kTone, kHighCut,
        kMix, kOutGain, kAutoGain, kOversampling, kNumParams
    };

    pump::tube::Engine engine;
    pump::tube::Settings settings;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending

    // Card 5.33: OVERSAMPLING changes the pedal's own latency, and nothing compensates latency across slots, so a
    // change under a held note shifts everything after this pedal. It is held back until the input has been silent
    // for a couple of blocks — a change between phrases rather than in the middle of one.
    int pendingOversampling = -1;   // -1: nothing waiting
    int silentBlocks = 0;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
    std::vector<float> leftScratch;    // prepare()'s priming pass only (see the comment there)
};
} // namespace pb
