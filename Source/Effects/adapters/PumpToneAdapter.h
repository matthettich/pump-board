#pragma once
// Card 5.8 (phase 5): pump TONE behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's ToneParams.hpp (docs/PUMP_DSP_MAP.md is the reference).
// pump's own `bypass` and its three unused `audition*` placeholders are not exposed: the slot's switch is the
// board's bypass, and the placeholders are host-compatibility ghosts that do nothing.
#include "Effects/IEffect.h"
#include "ToneEngine.hpp"
#include <vector>

namespace pb
{
class PumpToneAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.tone"; }
    juce::String displayName() const override { return "pump TONE"; }
    std::vector<ParamInfo> params() const override;

    // pump TONE is an STFT EQ: prepare() takes the sample rate only, and the block size is not its business.
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The STFT size itself: 2048 frames (43 ms at 48k) at quality 0, 4096 (85 ms) at quality 1. That is a lot for
    // a pedal, which is why card 5.4's dry-path compensation and card 5.6's status line matter here in particular.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.1; }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::tone::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kRecover = 0, kTame, kBias, kBrighten, kBody, kSpeed, kAutoLevel, kOutGain, kMix, kQuality, kDelta,
        kNumParams
    };

    pump::tone::Engine engine;
    pump::tone::Settings settings;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
