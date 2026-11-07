#pragma once
// Card 5.10 (phase 5): pump LIMIT behind IEffect. pump's limiter has no Settings struct — it takes one setter per
// control — so the settings live here and reach the engine through applySettingsTo(), in one fixed order that the
// null test reuses. Param ids, ranges, defaults and units match pump's LimitParams.hpp.
// Not exposed: pump's `bypass` (the slot's switch is the board's), `target` / `target_custom` (a loudness readout
// for pump's own meter, not the sound) and pump's own `unity` is kept because it does change what you hear.
#include "Effects/IEffect.h"
#include "LimitEngine.hpp"
#include <vector>

namespace pb
{
class PumpLimitAdapter final : public IEffect
{
public:
    struct Settings
    {
        float gainDb = 0.0f, ceilingDb = -1.0f, lookaheadMs = 5.0f;
        float attack = 50.0f, releaseMs = 100.0f, linkTrans = 75.0f, linkRel = 100.0f;
        int style = pump::limit::kAllround, oversampling = 1, dither = 0, shaping = 1;
        bool truePeak = true, dc = false, unity = false;
    };

    juce::String typeId() const override { return "pump.limit"; }
    juce::String displayName() const override { return "pump LIMIT"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // Lookahead and oversampling: pump reconfigures on the audio thread at the next block start, so this can
    // change mid-stream exactly like HEAT's and TONE's latency.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const Settings& currentSettings() const noexcept { return settings; }

    // The one place the settings become engine calls. The null test hands its reference engine this same order.
    template <class EngineType>
    void applySettingsTo (EngineType& target) const
    {
        target.setGain (settings.gainDb);
        target.setCeiling (settings.ceilingDb);
        target.setStyle (settings.style);
        target.setAttack (settings.attack);
        target.setRelease (settings.releaseMs);
        target.setLinks (settings.linkTrans, settings.linkRel);
        target.setDC (settings.dc);
        target.setUnity (settings.unity);
        target.setDither (settings.dither, settings.shaping);
        target.setLookahead (settings.lookaheadMs);
        target.setTruePeak (settings.truePeak);
        target.setOversampling (settings.oversampling);
    }

private:
    enum ParamIndex
    {
        kGain = 0, kCeiling, kStyle, kLookahead, kAttack, kRelease, kLinkTrans, kLinkRel, kTruePeak,
        kOversampling, kDcFilter, kDither, kShaping, kUnity,
        kNumParams
    };

    pump::limit::Engine engine;
    Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
