#include "Effects/adapters/PumpToneAdapter.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpToneAdapter::params() const
{
    // pump's layout, symbol for symbol (ToneParams.hpp). Everything here is linear on the knob in pump as well.
    return {
        { "recover",    "Recover",      0.0f, 100.0f,  30.0f, "%",  false, 0.0f },
        { "tame",       "Tame",         0.0f, 100.0f,  30.0f, "%",  false, 0.0f },
        { "bias",       "Bias",      -100.0f, 100.0f,   0.0f, "%",  false, 0.0f },
        { "brighten",   "Brighten",  -100.0f, 100.0f,   0.0f, "%",  false, 0.0f },
        { "body",       "Body",      -100.0f, 100.0f,   0.0f, "%",  false, 0.0f },
        { "speed",      "Speed",        0.0f, 100.0f,  40.0f, "%",  false, 0.0f },
        { "auto_level", "Auto Level",   0.0f,   1.0f,   1.0f, "",   true,  0.0f },
        { "out_gain",   "Output Gain", -12.0f,  12.0f,   0.0f, "dB", false, 0.0f },
        { "mix",        "Mix",          0.0f, 100.0f, 100.0f, "%",  false, 0.0f },
        { "quality",    "Quality",      0.0f,   1.0f,   0.0f, "",   false, 0.0f },
        { "delta",      "Delta",        0.0f,   1.0f,   0.0f, "",   true,  0.0f },
    };
}

void PumpToneAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate);
    engine.set (settings);              // pump's plugin hands its settings over right after prepare
    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpToneAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate);
    engine.set (settings);
    dirty = true;
}

void PumpToneAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kRecover:   settings.recover   = juce::jlimit (0.0f, 100.0f, value); break;
        case kTame:      settings.tame      = juce::jlimit (0.0f, 100.0f, value); break;
        case kBias:      settings.bias      = juce::jlimit (-100.0f, 100.0f, value); break;
        case kBrighten:  settings.brighten  = juce::jlimit (-100.0f, 100.0f, value); break;
        case kBody:      settings.body      = juce::jlimit (-100.0f, 100.0f, value); break;
        case kSpeed:     settings.speed     = juce::jlimit (0.0f, 100.0f, value); break;
        case kAutoLevel: settings.autoLevel = value >= 0.5f; break;
        case kOutGain:   settings.outDb     = juce::jlimit (-12.0f, 12.0f, value); break;
        case kMix:       settings.mix       = juce::jlimit (0.0f, 100.0f, value); break;
        case kQuality:   settings.quality   = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kDelta:     settings.delta     = value >= 0.5f; break;
        default:         return;
    }

    dirty = true;
}

void PumpToneAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
