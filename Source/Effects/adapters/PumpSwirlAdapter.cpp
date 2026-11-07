#include "Effects/adapters/PumpSwirlAdapter.h"
#include "Effects/adapters/AssignHelpers.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpSwirlAdapter::params() const
{
    // pump's layout, symbol for symbol (SwirlParams.hpp). Log-scaled params carry their geometric midpoint as
    // skewCentre, so a knob's centre lands on the same value pump's does.
    return {
        { "mode",     "Type",        0.0f,   (float) (pump::swirl::kNumModes - 1), 1.0f,  "",   false, 0.0f , pb::choices::names (pump::swirl::kModeNames, 6) },
        { "variant",  "Variant",     0.0f,   3.0f,   0.0f, "",   false, 0.0f , pb::choices::names (pump::swirl::kVariantNames, 4) },
        { "rate",     "Rate",        0.02f,  12.0f,  0.5f, "Hz", false, std::sqrt (0.02f * 12.0f) },
        { "sync",     "Sync",        0.0f,   1.0f,   0.0f, "",   true,  0.0f },
        { "division", "Division",    0.0f,   (float) (pump::kNumNotes - 1), (float) pump::kNote1, "", false, 0.0f },
        { "depth",    "Depth",       0.0f,  100.0f,  60.0f, "%",  false, 0.0f },
        { "manual",   "Manual",      0.0f,  100.0f,  45.0f, "%",  false, 0.0f },
        { "feedback", "Feedback",  -95.0f,   95.0f,   0.0f, "%",  false, 0.0f },
        { "spread",   "Spread",      0.0f,  180.0f,  90.0f, "",   false, 0.0f },
        { "tone",     "Tone",     1000.0f, 20000.0f, 20000.0f, "Hz", false, std::sqrt (1000.0f * 20000.0f) },
        { "warmth",   "Warmth",      0.0f,  100.0f,  30.0f, "%",  false, 0.0f },
        { "mix",      "Mix",         0.0f,  100.0f,  50.0f, "%",  false, 0.0f },
        { "out_gain", "Output Gain", -24.0f, 24.0f,    0.0f, "dB", false, 0.0f },
    };
}

void PumpSwirlAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    transport.bpm = bpm;

    // Settings and transport before prepare: pump's prepare reads them to decide the starting type, so the
    // adapter starts on the settings it was given instead of fading from the defaults.
    engine.setSettings (settings);
    engine.setTransport (transport);
    engine.prepare (preparedRate, preparedBlock);

    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpSwirlAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise: after reset the adapter must be in exactly the state pump's engine is in after its
    // own prepare (the null test compares those two states, and the bucket-brigade hiss is seeded, so both
    // sides start from the same random sequence).
    engine.setSettings (settings);
    engine.setTransport (transport);
    engine.prepare (preparedRate, preparedBlock);
    dirty = true;
}

void PumpSwirlAdapter::setTempo (double beatsPerMinute) noexcept
{
    if (! std::isfinite (beatsPerMinute) || beatsPerMinute <= 0.0)
        return;

    bpm = beatsPerMinute;
    transport.bpm = bpm;
    engine.setTransport (transport);
}

void PumpSwirlAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kMode:     assignField (settings.mode, juce::jlimit (0, pump::swirl::kNumModes - 1, (int) std::lround (value)), dirty); break;
        case kVariant:  assignField (settings.variant, juce::jlimit (0, 3, (int) std::lround (value)), dirty); break;
        case kRate:     assignField (settings.rate, (double) value, dirty); break;
        case kSync:     assignField (settings.sync, value >= 0.5f, dirty); break;
        case kDivision: assignField (settings.division, juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)), dirty); break;
        case kDepth:    assignField (settings.depth, (double) value, dirty); break;
        case kManual:   assignField (settings.manual, (double) value, dirty); break;
        case kFeedback: assignField (settings.feedback, (double) value, dirty); break;
        case kSpread:   assignField (settings.spread, (double) value, dirty); break;
        case kTone:     assignField (settings.tone, (double) value, dirty); break;
        case kWarmth:   assignField (settings.warmth, (double) value, dirty); break;
        case kMix:      assignField (settings.mix, (double) value, dirty); break;
        case kOutGain:  assignField (settings.outDb, (double) value, dirty); break;
        default:        return;
    }

}

void PumpSwirlAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
