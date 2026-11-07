#pragma once
// Card 5.7 (phase 5): pump HEAT behind IEffect. pump's engine is used as-is; this file only translates.
// Param ids, ranges, defaults and units match pump's HeatParams.hpp (docs/PUMP_DSP_MAP.md is the reference).
// pump's own global `bypass` and its unused `listen_band` placeholder are not exposed: the slot's switch is the
// board's bypass, and the placeholder is a host-compatibility ghost. Every band's own bypass is kept.
#include "Effects/IEffect.h"
#include "HeatEngine.hpp"
#include <vector>

namespace pb
{
class PumpHeatAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.heat"; }
    juce::String displayName() const override { return "pump HEAT"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // The splitter (0 in analog mode) plus the oversampler: 4x is pump's default, so a HEAT slot is late unless
    // the player turns oversampling off. Card 5.4 compensates the dry paths; card 5.6 shows the total.
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.1; }

    // pump's engine takes its settings through setters rather than one struct, so there is no currentSettings()
    // to copy. The null test reads these and makes the same calls on pump's engine.
    const pump::heat::BandSettings& bandSettings (int index) const noexcept { return bands[(size_t) index]; }
    int bandCount() const noexcept { return bandCountValue; }
    float crossover (int index) const noexcept { return xover[(size_t) index]; }
    bool autoGainOn() const noexcept { return autoGainOnValue; }
    int oversamplingIndex() const noexcept { return osIndex; }
    int crossoverMode() const noexcept { return xoverModeValue; }
    float mixPercent() const noexcept { return mixValue; }
    float outputDb() const noexcept { return outDbValue; }

private:
    enum GlobalIndex
    {
        kOutGain = 0, kMix, kOversampling, kAutoGain, kXoverMode, kBands, kXover1, kXover2, kXover3,
        kFirstBand
    };

    static constexpr int kBandFields = 7;      // mode, drive, mix, level, solo, mute, bypass
    static constexpr int kNumBands = pump::heat::kNumBands;

    void applySettings() noexcept;             // every setter pump's engine has, in one place

    pump::heat::Engine engine;
    pump::heat::BandSettings bands[kNumBands] {};
    float xover[3] { 150.0f, 3000.0f, 9000.0f };
    int bandCountValue = 3;
    int osIndex = 2;                           // pump's default: 4x
    int xoverModeValue = 0;
    bool autoGainOnValue = true;
    float mixValue = 100.0f;
    float outDbValue = 0.0f;
    bool dirty = true;                         // pump applies a settings change on its next control tick
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;           // only used when the host hands us a single channel
};
} // namespace pb
