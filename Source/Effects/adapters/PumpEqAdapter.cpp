#include "Effects/adapters/PumpEqAdapter.h"
#include "Effects/adapters/AssignHelpers.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpEqAdapter::params() const
{
    // pump's global layout (EQParams.hpp), then this adapter's eight-band block.
    std::vector<ParamInfo> v = {
        { "out_gain",   "Output Gain",   -36.0f, 36.0f,   0.0f, "dB", false, 0.0f },
        { "gain_scale", "Gain Scale",   -100.0f, 200.0f, 100.0f, "%",  false, 0.0f },
        { "auto_gain",  "Auto Gain",       0.0f, 1.0f,    0.0f, "",   true,  0.0f },
        { "phase",      "Phase Mode",      0.0f, 1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::eq::kPhaseNames, 2) },
        { "lin_quality","Linear Phase Quality", 0.0f, 3.0f, 1.0f, "", false, 0.0f , pb::choices::names (pump::eq::kQualityNames, 4) },
        { "dyn_attack", "Dynamic Attack",  0.5f, 200.0f,  10.0f, "ms", false, 10.0f },
        { "dyn_release","Dynamic Release", 10.0f, 2000.0f, 120.0f, "ms", false, 141.421f },
    };

    for (int b = 0; b < kExposedBands; ++b)
    {
        const juce::String n (b + 1);
        const auto& defaults = settings.band[b];

        v.push_back ({ "b" + n + "_used",  "Band " + n + " Used",      0.0f, 1.0f,    defaults.used ? 1.0f : 0.0f, "",   true,  0.0f });
        v.push_back ({ "b" + n + "_shape", "Band " + n + " Shape",     0.0f, 7.0f,    (float) defaults.shape, "",   false, 0.0f, pb::choices::names (pump::eq::kShapeNames, 8) });
        v.push_back ({ "b" + n + "_freq",  "Band " + n + " Frequency", 10.0f, 30000.0f, defaults.freq, "Hz", false, 547.723f });
        v.push_back ({ "b" + n + "_gain",  "Band " + n + " Gain",    -30.0f, 30.0f,   defaults.gainDb, "dB", false, 0.0f });
        v.push_back ({ "b" + n + "_q",     "Band " + n + " Q",        0.025f, 40.0f,  defaults.q, "",   false, 1.0f });
        v.push_back ({ "b" + n + "_dyn",   "Band " + n + " Dynamic",   0.0f, 1.0f,    defaults.dyn ? 1.0f : 0.0f, "", true, 0.0f });
    }

    return v;
}

void PumpEqAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;
    prepared = true;
}

void PumpEqAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;
}

void PumpEqAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    if (index >= kBandParams && index < kNumParams)
    {
        const int offset = index - kBandParams;
        const int b = offset / kPerBandParams;
        const int field = offset % kPerBandParams;

        if (b < 0 || b >= kExposedBands)
            return;

        auto& band = settings.band[b];

        switch (field)
        {
            case 0: assignField (band.used, value >= 0.5f, dirty); break;
            case 1: assignField (band.shape, juce::jlimit (0, (int) pump::Shape::Count - 1, (int) std::lround (value)), dirty); break;
            case 2: assignField (band.freq, juce::jlimit (10.0f, 30000.0f, value), dirty); break;
            case 3: assignField (band.gainDb, juce::jlimit (-30.0f, 30.0f, value), dirty); break;
            case 4: assignField (band.q, juce::jlimit (0.025f, 40.0f, value), dirty); break;
            case 5: assignField (band.dyn, value >= 0.5f, dirty); break;
            default: return;
        }
        return;
    }

    switch (index)
    {
        case kOutGain:    assignField (settings.outDb, juce::jlimit (-36.0f, 36.0f, value), dirty); break;
        case kGainScale:  assignField (settings.gainScale, juce::jlimit (-100.0f, 200.0f, value), dirty); break;
        case kAutoGain:   assignField (settings.autoGain, value >= 0.5f, dirty); break;
        case kPhase:      assignField (settings.phase, juce::jlimit (0, 1, (int) std::lround (value)), dirty); break;
        case kLinQuality: assignField (settings.linQuality, juce::jlimit (0, 3, (int) std::lround (value)), dirty); break;
        case kDynAttack:  assignField (settings.dynAttackMs, juce::jlimit (0.5f, 200.0f, value), dirty); break;
        case kDynRelease: assignField (settings.dynReleaseMs, juce::jlimit (10.0f, 2000.0f, value), dirty); break;
        default:          return;
    }

}

void PumpEqAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        applySettingsTo (engine);
        dirty = false;
    }

    auto* left = buffer.getWritePointer (0);
    float* right = nullptr;

    if (numChannels > 1)
    {
        right = buffer.getWritePointer (1);
    }
    else
    {
        if ((size_t) numSamples > rightScratch.size())
            return;

        std::memcpy (rightScratch.data(), left, (size_t) numSamples * sizeof (float));
        right = rightScratch.data();
    }

    engine.process (left, right, numSamples);

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
