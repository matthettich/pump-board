#pragma once
// Card 5.1 (phase 5): pump CRUSH behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's CrushParams.hpp exactly (docs/PUMP_DSP_MAP.md is the
// reference). pump's own `bypass` param is not duplicated: the slot's on/off switch is the board's bypass.
#include "Effects/IEffect.h"
#include "CrushEngine.hpp"
#include <vector>

namespace pb
{
class PumpCrushAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.crush"; }
    juce::String displayName() const override { return "pump CRUSH"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // pump CRUSH's media stage runs a fixed 6 ms transport, and the dry path is delayed by the same amount so a
    // parallel crush lines up with itself. The whole effect is therefore 6 ms late, not just its wet part —
    // which the chain has to compensate for (see NOTES.md, card 5.1).
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.05; }      // the transport and the filters, nothing more

    // The settings this adapter hands pump's engine; the null test drives pump's engine with the same struct.
    const pump::crush::Settings& currentSettings() const noexcept { return settings; }

private:
    enum ParamIndex
    {
        kMode = 0, kDrive, kBits, kRate, kJitter, kOrder, kWow, kFlutter, kHiss, kCrackle, kDropout, kAge,
        kLowCut, kHighCut, kMix, kOutGain, kNumParams
    };

    pump::crush::Engine engine;
    pump::crush::Settings settings;
    bool dirty = true;          // pump applies a settings change on its next control tick; start with one pending
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;   // only used when the host hands us a single channel
};
} // namespace pb
