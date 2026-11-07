#include "Effects/adapters/PumpSnapAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpSnapAdapter::params() const
{
    // pump's layout, symbol for symbol (SnapParams.hpp), minus bypass.
    return {
        { "attack",     "Attack",         -100.0f, 100.0f,   0.0f,  "%",  false, 0.0f },
        { "sustain",    "Sustain",        -100.0f, 100.0f,   0.0f,  "%",  false, 0.0f },
        { "attack_time","Attack Time",       1.0f, 100.0f,  10.0f,  "ms", false, 10.0f },
        { "release",    "Release",          20.0f, 2000.0f, 200.0f, "ms", false, 200.0f },
        { "link",       "Stereo Link",       0.0f, 100.0f, 100.0f,  "%",  false, 0.0f },
        { "focus",      "Detector Focus",    0.0f, (float) (pump::snap::kNumFocus - 1), (float) pump::snap::kFocusFull, "", false, 0.0f , pb::choices::names (pump::snap::kFocusNames, 4) },
        { "band",       "Band",              0.0f, (float) (pump::snap::kNumBands - 1), (float) pump::snap::kBandFull, "", false, 0.0f , pb::choices::names (pump::snap::kBandNames, 3) },
        { "crossover",  "Crossover",        40.0f, 8000.0f,  250.0f, "Hz", false, 565.685f },
        { "lookahead",  "Lookahead",         0.0f, (float) pump::snap::kMaxLookaheadMs, 0.0f, "ms", false, 0.0f },
        { "clip",       "Clip",              0.0f, 1.0f,      0.0f,  "",   true,  0.0f },
        { "ceiling",    "Clip Ceiling",    -12.0f, 0.0f,     -1.0f,  "dB", false, 0.0f },
        { "delta",      "Delta",             0.0f, 1.0f,      0.0f,  "",   true,  0.0f },
        { "mix",        "Mix",               0.0f, 100.0f,  100.0f,  "%",  false, 0.0f },
        { "out_gain",   "Output Gain",     -24.0f, 24.0f,     0.0f,  "dB", false, 0.0f },
    };
}

void PumpSnapAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);      // pump's plugin hands its settings over right after prepare
    dirty = true;                       // and leaves one change pending
    prepared = true;
}

void PumpSnapAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    dirty = true;
}

void PumpSnapAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kAttack:     settings.attack      = juce::jlimit (-100.0, 100.0, (double) value); break;
        case kSustain:    settings.sustain     = juce::jlimit (-100.0, 100.0, (double) value); break;
        case kAttackTime: settings.attackMs    = juce::jlimit (1.0, 100.0, (double) value); break;
        case kRelease:    settings.releaseMs   = juce::jlimit (20.0, 2000.0, (double) value); break;
        case kLink:       settings.link        = juce::jlimit (0.0, 100.0, (double) value); break;
        case kFocus:      settings.focus       = juce::jlimit (0, pump::snap::kNumFocus - 1, (int) std::lround (value)); break;
        case kBand:       settings.band        = juce::jlimit (0, pump::snap::kNumBands - 1, (int) std::lround (value)); break;
        case kCrossover:  settings.crossover   = juce::jlimit (40.0, 8000.0, (double) value); break;
        case kLookahead:  settings.lookaheadMs = juce::jlimit (0.0, (double) pump::snap::kMaxLookaheadMs, (double) value); break;
        case kClip:       settings.clip        = value >= 0.5f; break;
        case kCeiling:    settings.ceilingDb   = juce::jlimit (-12.0, 0.0, (double) value); break;
        case kDelta:      settings.delta       = value >= 0.5f; break;
        case kMix:        settings.mix         = juce::jlimit (0.0, 100.0, (double) value); break;
        case kOutGain:    settings.outDb       = juce::jlimit (-24.0, 24.0, (double) value); break;
        default:          return;
    }

    dirty = true;
}

void PumpSnapAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        engine.setSettings (settings);      // block start, as pump's engine expects
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
