#include "Effects/adapters/PumpCrushAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpCrushAdapter::params() const
{
    // pump's layout, symbol for symbol (CrushParams.hpp). Log-scaled params carry their geometric midpoint as
    // skewCentre, so a knob's centre lands on the same value pump's does.
    return {
        { "mode",     "Voicing",      0.0f,     6.0f,     1.0f, "",   false, 0.0f , pb::choices::names (pump::crush::kModeNames, 7) },
        { "drive",    "Drive",        0.0f,    24.0f,     0.0f, "dB", false, 0.0f },
        { "bits",     "Bits",         1.0f,    24.0f,    12.0f, "",   false, 0.0f },
        { "rate",     "Sample Rate", 200.0f, 48000.0f, 26040.0f, "Hz", false, std::sqrt (200.0f * 48000.0f) },
        { "jitter",   "Jitter",       0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "order",    "Media",        0.0f,     1.0f,     0.0f, "",   false, 0.0f , pb::choices::names (pump::crush::kOrderNames, 2) },
        { "wow",      "Wow",          0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "flutter",  "Flutter",      0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "hiss",     "Hiss",         0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "crackle",  "Crackle",      0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "dropout",  "Dropouts",     0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "age",      "Age",          0.0f,   100.0f,     0.0f, "%",  false, 0.0f },
        { "low_cut",  "Low Cut",     10.0f,  1000.0f,    10.0f, "Hz", false, std::sqrt (10.0f * 1000.0f) },
        { "high_cut", "High Cut",  1000.0f, 22000.0f, 22000.0f, "Hz", false, std::sqrt (1000.0f * 22000.0f) },
        { "mix",      "Mix",          0.0f,   100.0f,   100.0f, "%",  false, 0.0f },
        { "out_gain", "Output Gain", -24.0f,   24.0f,     0.0f, "dB", false, 0.0f },
    };
}

void PumpCrushAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpCrushAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise rather than only clearing state: after reset the adapter must be in exactly the
    // state pump's engine is in after its own prepare (the null test compares those two states, and the noise
    // stages are seeded, so both sides start from the same random sequence).
    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    dirty = true;
}

void PumpCrushAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kMode:     settings.mode    = juce::jlimit (0, pump::crush::kNumModes - 1, (int) std::lround (value)); break;
        case kDrive:    settings.drive   = (double) value; break;
        case kBits:     settings.bits    = (double) value; break;
        case kRate:     settings.rate    = (double) value; break;
        case kJitter:   settings.jitter  = (double) value; break;
        case kOrder:    settings.order   = juce::jlimit (0, pump::crush::kNumOrders - 1, (int) std::lround (value)); break;
        case kWow:      settings.wow     = (double) value; break;
        case kFlutter:  settings.flutter = (double) value; break;
        case kHiss:     settings.hiss    = (double) value; break;
        case kCrackle:  settings.crackle = (double) value; break;
        case kDropout:  settings.dropout = (double) value; break;
        case kAge:      settings.age     = (double) value; break;
        case kLowCut:   settings.lowCut  = (double) value; break;
        case kHighCut:  settings.highCut = (double) value; break;
        case kMix:      settings.mix     = (double) value; break;
        case kOutGain:  settings.outDb   = (double) value; break;
        default:        return;
    }

    dirty = true;
}

void PumpCrushAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        engine.setSettings (settings);   // block start, as pump's engine expects
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
            return;                      // never take a block bigger than prepare() promised

        std::memcpy (rightScratch.data(), left, (size_t) numSamples * sizeof (float));
        right = rightScratch.data();
    }

    engine.process (left, right, numSamples);

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
