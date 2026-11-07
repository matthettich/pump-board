#pragma once
// Card 5.13 (phase 5): pump CALM behind IEffect — a resonance and harshness catcher: it finds the narrow peaks that
// make a guitar or a mix sound hard and pulls them down, without touching the broad balance of the sound.
// pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units match
// pump's CalmParams.hpp. pump's own `bypass` and its three `audition*` placeholders are not exposed.
#include "Effects/IEffect.h"
#include "CalmEngine.hpp"
#include <vector>

namespace pb
{
class PumpCalmAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.calm"; }
    juce::String displayName() const override { return "pump CALM"; }
    std::vector<ParamInfo> params() const override;

    // pump CALM is an STFT effect: prepare() takes the sample rate only, and the block size is not its business.
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The FFT size, like TONE: real latency, and QUALITY changes it mid-stream.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.05; }

    const pump::calm::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kDepth = 0, kSharpness, kSelectivity, kAttack, kRelease, kMode, kFocusLow, kFocusHigh, kStereo,
        kBalance, kQuality, kDelta, kMix, kTrim,
        kNumParams
    };

    pump::calm::Engine engine;
    pump::calm::Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
