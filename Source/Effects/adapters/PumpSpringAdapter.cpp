#include "Effects/adapters/PumpSpringAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpSpringAdapter::params() const
{
    // pump's layout, symbol for symbol (SpringParams.hpp). Log-scaled params carry their geometric
    // midpoint as skewCentre, so a knob's centre lands on the same value pump's does.
    return {
        { "model",     "Model",        0.0f,    5.0f,   0.0f, "",   false, 0.0f , pb::choices::names (pump::spring::kModelNames, 6) },
        { "dwell",     "Dwell",        0.0f,  100.0f,  50.0f, "%",  false, 0.0f },
        { "decay",     "Decay",        0.3f,    8.0f,   2.5f, "s",  false, std::sqrt (0.3f * 8.0f) },
        { "tension",   "Tension",   -100.0f,  100.0f,   0.0f, "%",  false, 0.0f },
        { "drip",      "Drip",         0.0f,  100.0f,  50.0f, "%",  false, 0.0f },
        { "tone",      "Tone",      -100.0f,  100.0f,   0.0f, "%",  false, 0.0f },
        { "low_cut",   "Low Cut",     20.0f, 1000.0f, 100.0f, "Hz", false, std::sqrt (20.0f * 1000.0f) },
        { "pre_delay", "Pre-Delay",    0.0f,  250.0f,   0.0f, "ms", false, 0.0f },
        { "wobble",    "Wobble",       0.0f,  100.0f,  15.0f, "%",  false, 0.0f },
        { "width",     "Width",        0.0f,  100.0f,  70.0f, "%",  false, 0.0f },
        { "grit",      "Grit",         0.0f,  100.0f,  35.0f, "%",  false, 0.0f },
        { "mix",       "Mix",          0.0f,  100.0f,  30.0f, "%",  false, 0.0f },
        { "out_gain",  "Output Gain", -24.0f,   24.0f,   0.0f, "dB", false, 0.0f },
    };
}

void PumpSpringAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
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

void PumpSpringAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise rather than only clearing the tank: after reset the adapter must be in exactly the
    // state pump's engine is in after its own prepare (the null test compares those two states).
    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    dirty = true;
}

void PumpSpringAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kModel:     settings.model   = juce::jlimit (0, pump::spring::kNumModels - 1, (int) std::lround (value)); break;
        case kDwell:     settings.dwell   = (double) value; break;
        case kDecay:     settings.decay   = (double) value; break;
        case kTension:   settings.tension = (double) value; break;
        case kDrip:      settings.drip    = (double) value; break;
        case kTone:      settings.tone    = (double) value; break;
        case kLowCut:    settings.lowCut  = (double) value; break;
        case kPreDelay:  settings.preDelay = (double) value; break;
        case kWobble:    settings.wobble  = (double) value; break;
        case kWidth:     settings.width   = (double) value; break;
        case kGrit:      settings.grit    = (double) value; break;
        case kMix:       settings.mix     = (double) value; break;
        case kOutGain:   settings.outDb   = (double) value; break;
        default:         return;
    }

    dirty = true;
}

void PumpSpringAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
