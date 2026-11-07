#pragma once
// Card 1.4: pump ECHO behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's EchoParams.hpp exactly (docs/PUMP_DSP_MAP.md is the reference).
// pump's own `bypass` param is not duplicated: the slot's on/off switch is the board's bypass.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "EchoEngine.hpp"
#include <vector>

namespace pb
{
class PumpEchoAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.echo"; }
    juce::String displayName() const override { return "pump ECHO"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }      // the dry signal is the input itself
    double tailSeconds() const noexcept override { return 8.0; }    // the longest line

    // There is no host transport in a standalone: the board supplies the tempo the synced times use.
    void setTempo (double beatsPerMinute) noexcept override;
    double tempo() const noexcept { return bpm; }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::echo::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kMode = 0, kTimeL, kTimeR, kSync, kNoteL, kNoteR, kLink, kFeedback, kStereo, kWidth,
        kModDepth, kModRate, kAge, kDrive, kLowCut, kHighCut, kDiffuse, kDuck, kFreeze,
        kPitch, kFine, kPitchMode, kMix, kOutGain, kNumParams
    };

    pump::echo::Engine engine;
    pump::echo::Settings settings;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    double bpm = 120.0;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
