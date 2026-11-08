#include "Effects/adapters/PumpCalmAdapter.h"
#include "Effects/adapters/AssignHelpers.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpCalmAdapter::params() const
{
    // pump's layout, symbol for symbol (CalmParams.hpp), minus bypass and the audition placeholders.
    return {
        { "depth",      "Depth",          0.0f, 100.0f,   50.0f, "%",  false, 0.0f },
        { "sharpness",  "Sharpness",      0.0f, 100.0f,   50.0f, "%",  false, 0.0f },
        { "selectivity","Selectivity",    0.0f, 100.0f,   50.0f, "%",  false, 0.0f },
        { "attack",     "Attack",         1.0f, 200.0f,    8.0f, "ms", false, 14.142f },
        { "release",    "Release",       10.0f, 1000.0f, 120.0f, "ms", false, 100.0f },
        { "mode",       "Mode",           0.0f, 1.0f, (float) pump::calm::kSoft, "", false, 0.0f , pb::choices::names (pump::calm::kModeNames, 2) },
        { "focus_low",  "Focus Low",     20.0f, 20000.0f,  80.0f, "Hz", false, 632.455f },
        { "focus_high", "Focus High",    20.0f, 20000.0f, 20000.0f, "Hz", false, 632.455f },
        { "stereo",     "Stereo Mode",    0.0f, 2.0f, (float) pump::calm::kLinked, "", false, 0.0f , pb::choices::names (pump::calm::kStereoNames, 3) },
        { "balance",    "Stereo Balance",-100.0f, 100.0f,   0.0f, "%",  false, 0.0f },
        { "quality",    "Quality",        0.0f, 3.0f,      1.0f, "",   false, 0.0f , pb::choices::names (pump::calm::kQualityNames, 4) },
        { "delta",      "Delta",          0.0f, 1.0f,      0.0f, "",   true,  0.0f },
        { "mix",        "Mix",            0.0f, 100.0f,  100.0f, "%",  false, 0.0f },
        { "trim",       "Output Trim",  -12.0f, 12.0f,     0.0f, "dB", false, 0.0f },
    };
}

void PumpCalmAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate);      // pump CALM takes the sample rate only
    engine.set (settings);              // pump's plugin hands its settings over right after prepare
    dirty = true;
    prepared = true;
}

void PumpCalmAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate);
    engine.set (settings);
    dirty = true;
}

void PumpCalmAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kDepth:       assignField (settings.depth, juce::jlimit (0.0f, 100.0f, value), dirty); break;
        case kSharpness:   assignField (settings.sharpness, juce::jlimit (0.0f, 100.0f, value), dirty); break;
        case kSelectivity: assignField (settings.selectivity, juce::jlimit (0.0f, 100.0f, value), dirty); break;
        case kAttack:      assignField (settings.attackMs, juce::jlimit (1.0f, 200.0f, value), dirty); break;
        case kRelease:     assignField (settings.releaseMs, juce::jlimit (10.0f, 1000.0f, value), dirty); break;
        case kMode:        assignField (settings.mode, juce::jlimit (0, 1, (int) std::lround (value)), dirty); break;
        case kFocusLow:    assignField (settings.focusLo, juce::jlimit (20.0f, 20000.0f, value), dirty); break;
        case kFocusHigh:   assignField (settings.focusHi, juce::jlimit (20.0f, 20000.0f, value), dirty); break;
        case kStereo:      assignField (settings.stereo, juce::jlimit (0, 2, (int) std::lround (value)), dirty); break;
        case kBalance:     assignField (settings.balance, juce::jlimit (-100.0f, 100.0f, value), dirty); break;
        case kQuality:     assignField (settings.quality, juce::jlimit (0, 3, (int) std::lround (value)), dirty); break;
        case kDelta:       assignField (settings.delta, value >= 0.5f, dirty); break;
        case kMix:         assignField (settings.mix, juce::jlimit (0.0f, 100.0f, value), dirty); break;
        case kTrim:        assignField (settings.trimDb, juce::jlimit (-12.0f, 12.0f, value), dirty); break;
        default:           return;
    }

}

void PumpCalmAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        engine.set (settings);          // block start, as pump's engine expects
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
