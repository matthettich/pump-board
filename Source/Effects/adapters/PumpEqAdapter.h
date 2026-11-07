#pragma once
// Card 5.21 (phase 5): pump EQ behind IEffect — a 24-band dynamic EQ. pump's engine has no Settings struct: it takes
// the globals through setters and every band through setBand(), so the settings live here and reach the engine
// through applySettingsTo(), one fixed order the null test reuses. Param ids, ranges, defaults and units match
// pump's EQParams.hpp. Not exposed: pump's `bypass`, its three `audition*` placeholders, `analyzer` and `solo`
// (both are a display/audition feature of pump's own editor, not the sound).
//
// **Deviation:** pump offers 24 bands; a slot editor here is a knob grid, so this adapter exposes the first
// **8** bands (used, shape, frequency, gain, Q, dynamic) and leaves 9..24 unused, which is exactly how pump ships
// them. Each band's slope (12 dB/oct), stereo placement, dynamic range and dynamic threshold stay on pump's defaults.
// docs/NOTES.md records it.
#include "Effects/IEffect.h"
#include "EQEngine.hpp"
#include <vector>

namespace pb
{
class PumpEqAdapter final : public IEffect
{
public:
    static constexpr int kExposedBands = 8;
    static constexpr int kPerBandParams = 6;    // used, shape, frequency, gain, Q, dynamic

    struct Band
    {
        bool used = false;
        int shape = 0;                          // pump::Shape, in pump's kShapeNames order
        float freq = 1000.0f, gainDb = 0.0f, q = 10.0f;
        bool dyn = false;
        float dynRange = -6.0f, dynThresh = -30.0f;
    };

    struct Settings
    {
        float outDb = 0.0f, gainScale = 100.0f, dynAttackMs = 10.0f, dynReleaseMs = 120.0f;
        int phase = 0, linQuality = 1;
        bool autoGain = false;
        Band band[kExposedBands];
    };

    juce::String typeId() const override { return "pump.eq"; }
    juce::String displayName() const override { return "pump EQ"; }
    std::vector<ParamInfo> params() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void setParam (int index, float value) noexcept override;
    void process (juce::AudioBuffer<float>& buffer) noexcept override;

    // Zero in pump's default zero-latency mode; the linear-phase mode reports its kernel length, and switching
    // between the two moves the figure mid-stream (the linear-phase kernels are rebuilt on a worker thread).
    int latencySamples() const noexcept override { return prepared ? engine.latency() : 0; }
    double tailSeconds() const noexcept override { return 0.0; }

    const Settings& currentSettings() const noexcept { return settings; }

    template <class EngineType>
    void applySettingsTo (EngineType& target) const
    {
        target.setGainScale (settings.gainScale);
        target.setOutput (settings.outDb);
        target.setAutoGain (settings.autoGain);
        target.setDynTimes (settings.dynAttackMs, settings.dynReleaseMs);
        target.setSolo (-1);
        target.setPhaseMode (settings.phase, settings.linQuality);

        for (int b = 0; b < kExposedBands; ++b)
        {
            const auto& band = settings.band[b];

            pump::BandSpec spec;
            spec.shape = (pump::Shape) band.shape;
            spec.freq = (double) band.freq;
            spec.gainDb = (double) band.gainDb;
            spec.q = (double) band.q;
            spec.slope = 12;

            target.setBand (b, band.used, false, spec, 0, band.dyn, band.dynRange, band.dynThresh);
        }
    }

private:
    enum ParamIndex
    {
        kOutGain = 0, kGainScale, kAutoGain, kPhase, kLinQuality, kDynAttack, kDynRelease,
        kBandParams,            // base of the per-band block
        kNumParams = kBandParams + kExposedBands * kPerBandParams
    };

    pump::eq::Engine engine;
    Settings settings;
    bool dirty = true;
    bool prepared = false;
    double preparedRate = 48000.0;
    int preparedBlock = 512;
    std::vector<float> rightScratch;
};
} // namespace pb
