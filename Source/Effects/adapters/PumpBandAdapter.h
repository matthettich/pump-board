#pragma once
// Card 5.20 (phase 5): pump BAND behind IEffect — a multiband compressor: 1..4 bands with three crossovers, per-band
// threshold, ratio, attack, release and makeup, peak or RMS detection, stereo link, an optional lookahead and auto
// makeup. pump's engine has no Settings struct — one setter per control, plus the per-band BandSettings — so the
// settings live here and reach the engine through applySettingsTo(), one fixed order the null test reuses.
// Param ids, ranges, defaults and units match pump's BandParams.hpp. Not exposed: pump's `bypass` and hidden
// `listen_band`.
//
// **Deviation:** pump's own parameter table has no per-band controls at all (its editor writes BandSettings directly),
// so the per-band params here are this adapter's own layout: threshold, ratio, attack, release and makeup for each of
// the four bands, in pump's units. Band mode, knee, range, solo, mute and band bypass stay on pump's defaults.
#include "Effects/IEffect.h"
#include "BandEngine.hpp"
#include <vector>

namespace pb
{
class PumpBandAdapter final : public IEffect
{
public:
    static constexpr int kPerBandParams = 5;    // threshold, ratio, attack, release, makeup

    struct Settings
    {
        float outDb = 0.0f, mix = 100.0f, link = 100.0f, lookaheadMs = 0.0f;
        int bands = 3, xoverMode = 0, detection = 0;            // 0 = Peak, 1 = RMS
        float xover[3] = { 120.0f, 2000.0f, 8000.0f };
        bool autoMakeup = false;
        pump::band::BandSettings band[pump::band::kNumBands];
    };

    juce::String typeId() const override { return "pump.band"; }
    juce::String displayName() const override { return "pump BAND"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const Settings& currentSettings() const noexcept { return settings; }

    template <class EngineType>
    void applySettingsTo (EngineType& target) const
    {
        target.setBands (settings.bands);
        target.setCrossovers (settings.xover[0], settings.xover[1], settings.xover[2]);
        target.setXoverMode (settings.xoverMode);
        target.setLookahead (settings.lookaheadMs);
        target.setDetection (settings.detection);
        target.setLink (settings.link);
        target.setMix (settings.mix);
        target.setOutput (settings.outDb);
        target.setAutoMakeup (settings.autoMakeup);
        target.setListen (-1);

        for (int b = 0; b < pump::band::kNumBands; ++b)
            target.setBand (b, settings.band[b]);
    }

private:
    enum ParamIndex
    {
        kOutGain = 0, kMix, kXoverMode, kBands, kXover1, kXover2, kXover3, kLookahead, kDetection, kStereoLink,
        kAutoMakeup,
        kBandParams,            // base of the per-band block
        kNumParams = kBandParams + pump::band::kNumBands * kPerBandParams
    };

    pump::band::Engine engine;
    Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
