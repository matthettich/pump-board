#include "Effects/adapters/PumpLimitAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpLimitAdapter::params() const
{
    // pump's layout, symbol for symbol (LimitParams.hpp), minus bypass / target / target_custom.
    return {
        { "gain",        "Gain",           0.0f,  30.0f,   0.0f, "dB", false, 0.0f },
        { "ceiling",     "Output Ceiling", -12.0f, 0.0f,   -1.0f, "dB", false, 0.0f },
        { "style",       "Style",           0.0f,  (float) (pump::limit::kNumStyles - 1), (float) pump::limit::kAllround, "", false, 0.0f , pb::choices::names (pump::limit::kStyleNames, 7) },
        { "lookahead",   "Lookahead",       0.1f,  10.0f,   5.0f, "ms", false, 1.0f },
        { "attack",      "Attack",          0.0f,  100.0f, 50.0f, "%",  false, 0.0f },
        { "release",     "Release",         1.0f,  1000.0f, 100.0f, "ms", false, 31.622776f },
        { "link_trans",  "Channel Link Transients", 0.0f, 100.0f, 75.0f, "%", false, 0.0f },
        { "link_release","Channel Link Release",    0.0f, 100.0f, 100.0f, "%", false, 0.0f },
        { "true_peak",   "True Peak Limiting", 0.0f, 1.0f,   1.0f, "",   true,  0.0f },
        { "oversampling","Oversampling",     0.0f,  3.0f,   1.0f, "",   false, 0.0f , pb::choices::names (pump::limit::kOSNames, 4) },
        { "dc_filter",   "DC Offset Filter", 0.0f,  1.0f,   0.0f, "",   true,  0.0f },
        { "dither",      "Dither",           0.0f,  2.0f,   0.0f, "",   false, 0.0f , pb::choices::names (pump::limit::kDitherNames, 3) },
        { "noise_shaping","Noise Shaping",   0.0f,  1.0f,   1.0f, "",   false, 0.0f , pb::choices::names (pump::limit::kShapingNames, 2) },
        { "unity",       "Unity Gain Listen", 0.0f, 1.0f,   0.0f, "",   true,  0.0f },
    };
}

void PumpLimitAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;                      // the settings are in the engine already
    prepared = true;
}

void PumpLimitAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;
}

void PumpLimitAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kGain:        settings.gainDb      = juce::jlimit (0.0f, 30.0f, value); break;
        case kCeiling:     settings.ceilingDb   = juce::jlimit (-12.0f, 0.0f, value); break;
        case kStyle:       settings.style       = juce::jlimit (0, pump::limit::kNumStyles - 1, (int) std::lround (value)); break;
        case kLookahead:   settings.lookaheadMs = juce::jlimit (0.1f, 10.0f, value); break;
        case kAttack:      settings.attack      = juce::jlimit (0.0f, 100.0f, value); break;
        case kRelease:     settings.releaseMs   = juce::jlimit (1.0f, 1000.0f, value); break;
        case kLinkTrans:   settings.linkTrans   = juce::jlimit (0.0f, 100.0f, value); break;
        case kLinkRel:     settings.linkRel     = juce::jlimit (0.0f, 100.0f, value); break;
        case kTruePeak:    settings.truePeak    = value >= 0.5f; break;
        case kOversampling: settings.oversampling = juce::jlimit (0, 3, (int) std::lround (value)); break;
        case kDcFilter:    settings.dc          = value >= 0.5f; break;
        case kDither:      settings.dither      = juce::jlimit (0, 2, (int) std::lround (value)); break;
        case kShaping:     settings.shaping     = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kUnity:       settings.unity       = value >= 0.5f; break;
        default:           return;
    }

    dirty = true;
}

void PumpLimitAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        applySettingsTo (engine);       // block start, as pump's plugin does
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
