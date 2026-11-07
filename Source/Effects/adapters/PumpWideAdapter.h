#pragma once
// Card 5.19 (phase 5): pump WIDE behind IEffect — a multiband stereo widener: per-band width and stereoize amounts,
// mono bass below a crossover with an optional 2nd-order slope, balance, and 1..4 bands with three crossovers.
// pump's engine has no Settings struct — it takes one setter per control — so the settings live here and reach the
// engine through applySettingsTo(), in one fixed order the null test reuses. Param ids, ranges, defaults and units
// match pump's WideParams.hpp. Not exposed: pump's `bypass` and its hidden `listen_band`.
#include "Effects/IEffect.h"
#include "WideEngine.hpp"
#include <vector>

namespace pb
{
class PumpWideAdapter final : public IEffect
{
public:
    struct Settings
    {
        float outDb = 0.0f, balance = 0.0f, monoFreq = 120.0f;
        int monoSlope = 0, xoverMode = 0, bands = pump::wide::kNumBands;
        float xover[3] = { 120.0f, 1000.0f, 6000.0f };
        bool monoBass = false;
        pump::wide::BandSettings band[pump::wide::kNumBands];
    };

    juce::String typeId() const override { return "pump.wide"; }
    juce::String displayName() const override { return "pump WIDE"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const Settings& currentSettings() const noexcept { return settings; }

    // The one place the settings become engine calls; the null test hands its reference engine this same order.
    template <class EngineType>
    void applySettingsTo (EngineType& target) const
    {
        target.setBands (settings.bands);
        target.setCrossovers (settings.xover[0], settings.xover[1], settings.xover[2]);
        target.setXoverMode (settings.xoverMode);
        target.setMonoBass (settings.monoBass, settings.monoFreq, settings.monoSlope);
        target.setBalance (settings.balance);
        target.setOutput (settings.outDb);
        target.setListen (-1);

        for (int b = 0; b < pump::wide::kNumBands; ++b)
            target.setBand (b, settings.band[b]);
    }

private:
    enum ParamIndex
    {
        kOutGain = 0, kBalance, kMonoBass, kMonoFreq, kMonoSlope, kXoverMode, kBands, kXover1, kXover2, kXover3,
        kBand1Width, kBand1Stereoize,
        kBand2Width, kBand2Stereoize,
        kBand3Width, kBand3Stereoize,
        kBand4Width, kBand4Stereoize,
        kNumParams
    };

    pump::wide::Engine engine;
    Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
