#include "Effects/adapters/PumpGateAdapter.h"
#include "Effects/adapters/AssignHelpers.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpGateAdapter::params() const
{
    // pump's layout, symbol for symbol (GateParams.hpp), minus bypass and the per-step pattern.
    return {
        { "steps",        "Steps",           1.0f, (float) pump::gate::kMaxSteps, 16.0f, "",   false, 0.0f },
        { "rate",         "Rate",            0.0f, (float) (pump::kNumNotes - 1), (float) pump::kNote16, "", false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },
        { "swing",        "Swing",           0.0f, 75.0f,      0.0f, "%",  false, 0.0f },
        { "length",       "Gate Length",     5.0f, 100.0f,    75.0f, "%",  false, 0.0f },
        { "attack",       "Attack",          0.0f, 50.0f,      1.0f, "ms", false, 0.0f },
        { "release",      "Release",         0.0f, 500.0f,    20.0f, "ms", false, 0.0f },
        { "smooth",       "Smooth",          0.0f, 100.0f,     0.0f, "%",  false, 0.0f },
        { "depth",        "Depth",           0.0f, 100.0f,   100.0f, "%",  false, 0.0f },
        { "mode",         "Mode",            0.0f, (float) (pump::gate::kNumModes - 1), (float) pump::gate::kVolume, "", false, 0.0f , pb::choices::names (pump::gate::kModeNames, 3) },
        { "filter_low",   "Filter Low",     20.0f, 5000.0f,  250.0f, "Hz", false, 316.228f },
        { "filter_high",  "Filter High",   200.0f, 20000.0f, 20000.0f, "Hz", false, 2000.0f },
        { "resonance",    "Resonance",       0.0f, 100.0f,    25.0f, "%",  false, 0.0f },
        { "stereo_offset","Stereo Offset",   0.0f, 16.0f,      0.0f, "",   false, 0.0f },
        { "mix",          "Mix",             0.0f, 100.0f,   100.0f, "%",  false, 0.0f },
        { "out_gain",     "Output Gain",   -24.0f, 24.0f,      0.0f, "dB", false, 0.0f },
    };
}

void PumpGateAdapter::setTempo (double newBpm) noexcept
{
    bpm = juce::jlimit (20.0, 999.0, newBpm);
    transport.bpm = bpm;

    if (prepared)
        engine.setTransport (transport);
}

void PumpGateAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    transport.bpm = bpm;                // the board has no host: the sequencer free-runs at our own tempo
    transport.playing = false;
    transport.valid = false;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
    prepared = true;
}

void PumpGateAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
}

void PumpGateAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kSteps:       assignField (settings.steps, juce::jlimit (1, pump::gate::kMaxSteps, (int) std::lround (value)), dirty); break;
        case kRate:        assignField (settings.rate, juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)), dirty); break;
        case kSwing:       assignField (settings.swing, juce::jlimit (0.0, 75.0, (double) value), dirty); break;
        case kLength:      assignField (settings.length, juce::jlimit (5.0, 100.0, (double) value), dirty); break;
        case kAttack:      assignField (settings.attack, juce::jlimit (0.0, 50.0, (double) value), dirty); break;
        case kRelease:     assignField (settings.release, juce::jlimit (0.0, 500.0, (double) value), dirty); break;
        case kSmooth:      assignField (settings.smooth, juce::jlimit (0.0, 100.0, (double) value), dirty); break;
        case kDepth:       assignField (settings.depth, juce::jlimit (0.0, 100.0, (double) value), dirty); break;
        case kMode:        assignField (settings.mode, juce::jlimit (0, pump::gate::kNumModes - 1, (int) std::lround (value)), dirty); break;
        case kFilterLow:   assignField (settings.filterLow, juce::jlimit (20.0, 5000.0, (double) value), dirty); break;
        case kFilterHigh:  assignField (settings.filterHigh, juce::jlimit (200.0, 20000.0, (double) value), dirty); break;
        case kResonance:   assignField (settings.resonance, juce::jlimit (0.0, 100.0, (double) value), dirty); break;
        case kStereoOffset: assignField (settings.offset, juce::jlimit (0, 16, (int) std::lround (value)), dirty); break;
        case kMix:         assignField (settings.mix, juce::jlimit (0.0, 100.0, (double) value), dirty); break;
        case kOutGain:     assignField (settings.outDb, juce::jlimit (-24.0, 24.0, (double) value), dirty); break;
        default:           return;
    }

}

void PumpGateAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        engine.setSettings (settings);
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
