#pragma once
// Card 5.2 (phase 5): pump SWIRL behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's SwirlParams.hpp exactly (docs/PUMP_DSP_MAP.md is the
// reference). pump's own `bypass` param is not duplicated: the slot's on/off switch is the board's bypass.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "SwirlEngine.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pb
{
class PumpSwirlAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.swirl"; }
    juce::String displayName() const override { return "pump SWIRL"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // Every type is truly zero latency except the flanger's THRU-0 variant, whose dry copy is delayed by 3 ms so
    // the swept copy can cross it. The engine reports that, and it changes when the type/variant is switched.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.5; }      // the lines are at most 60 ms, with feedback

    // SWIRL locks its LFO to the song position when Sync is on; a standalone has no host transport, so the board
    // supplies a tempo and the LFO free-runs at the synced rate (playing = false, valid = false).
    void setTempo (double beatsPerMinute) noexcept override;
    double tempo() const noexcept { return bpm; }

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::swirl::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kMode = 0, kVariant, kRate, kSync, kDivision, kDepth, kManual, kFeedback,
        kSpread, kTone, kWarmth, kMix, kOutGain, kNumParams
    };

    pump::swirl::Engine engine;
    pump::swirl::Settings settings;
    pump::Transport transport;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    double bpm = 120.0;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
