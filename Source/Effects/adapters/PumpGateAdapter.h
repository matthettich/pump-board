#pragma once
// Card 5.14 (phase 5): pump GATE behind IEffect — a tempo-locked step gate: up to 32 steps of level, a note rate,
// swing, gate length, smoothing, a filter with resonance and three modes (level, filter, both).
// pump's engine is used as-is; this file only translates. Param ids, ranges, defaults and units match
// pump's GateParams.hpp. pump's own `bypass` is not exposed: the slot's switch is the board's bypass.
//
// The per-step pattern (32 levels) is not exposed: it is a curve, not a knob, and ParamInfo cannot describe it. The
// engine starts on pump's default pattern; docs/NOTES.md records this as the card's main deviation.
#include "Effects/IEffect.h"
#include "Effects/TempoAware.h"
#include "GateEngine.hpp"
#include "dsp/Tempo.hpp"
#include <vector>

namespace pb
{
class PumpGateAdapter final : public IEffect, public ITempoAware
{
public:
    juce::String typeId() const override { return "pump.gate"; }
    juce::String displayName() const override { return "pump GATE"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return 0; }     // pump GATE is a gain stage, not a delay
    double tailSeconds() const noexcept override { return 0.0; }

    const pump::gate::Settings& currentSettings() const noexcept { return settings; }
    const pump::Transport& currentTransport() const noexcept { return transport; }

    // The sequencer needs a tempo. The board has no host, so this stays at 120 BPM until the app grows one.
    double tempo() const noexcept { return bpm; }
    void setTempo (double newBpm) noexcept override;

private:
    enum ParamIndex
    {
        kSteps = 0, kRate, kSwing, kLength, kAttack, kRelease, kSmooth, kDepth, kMode, kFilterLow,
        kFilterHigh, kResonance, kStereoOffset, kMix, kOutGain,
        kNumParams
    };

    pump::gate::Engine engine;
    pump::gate::Settings settings;
    pump::Transport transport;
    double bpm = 120.0;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
