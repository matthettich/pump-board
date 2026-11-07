#include "Effects/adapters/PumpStutterAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpStutterAdapter::params() const
{
    // pump's layout, symbol for symbol (StutterParams.hpp), minus bypass.
    return {
        { "trigger",      "Trigger",         0.0f, (float) (pump::stutter::kNumTriggers - 1), (float) pump::stutter::kAuto, "", false, 0.0f , pb::choices::names (pump::stutter::kTriggerNames, 2) },
        { "interval",     "Interval",        0.0f, (float) (pump::kNumNotes - 1), (float) pump::kNote1, "", false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },
        { "chance",       "Chance",          0.0f, 100.0f,     35.0f, "%",  false, 0.0f },
        { "length",       "Length",          0.0f, (float) (pump::kNumNotes - 1), (float) pump::kNote4, "", false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },
        { "grid",         "Grid",            0.0f, (float) (pump::kNumNotes - 1), (float) pump::kNote16, "", false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },
        { "gate",         "Gate",            5.0f, 100.0f,    100.0f, "%",  false, 0.0f },
        { "pitch",        "Pitch per Repeat",-12.0f, 12.0f,     0.0f, "st", false, 0.0f },
        { "decay",        "Decay per Repeat", 0.0f, 100.0f,     0.0f, "%",  false, 0.0f },
        { "filter",       "Filter per Repeat",-100.0f, 100.0f,  0.0f, "%",  false, 0.0f },
        { "variation",    "Variation",       0.0f, 100.0f,      0.0f, "%",  false, 0.0f },
        { "tape",         "Tape Stop Time", 10.0f, 100.0f,    100.0f, "%",  false, 0.0f },
        { "mix",          "Mix",             0.0f, 100.0f,    100.0f, "%",  false, 0.0f },
        { "out_gain",     "Output Gain",   -24.0f, 24.0f,       0.0f, "dB", false, 0.0f },
        { "pattern_on",   "Pattern",         0.0f, 1.0f,        0.0f, "",   true,  0.0f },
        { "pattern",      "Pattern Select",  0.0f, (float) pump::stutter::kUserPattern, 1.0f, "", false, 0.0f },
        { "pattern_step", "Pattern Step",    0.0f, (float) (pump::kNumNotes - 1), (float) pump::kNote16, "", false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },
    };
}

void PumpStutterAdapter::setTempo (double newBpm) noexcept
{
    bpm = juce::jlimit (20.0, 999.0, newBpm);
    transport.bpm = bpm;

    if (prepared)
        engine.setTransport (transport);
}

void PumpStutterAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    transport.bpm = bpm;
    transport.playing = false;
    transport.valid = false;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
    prepared = true;
}

void PumpStutterAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
}

void PumpStutterAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kTrigger:     settings.trigger    = juce::jlimit (0, pump::stutter::kNumTriggers - 1, (int) std::lround (value)); break;
        case kInterval:    settings.interval   = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        case kChance:      settings.chance     = juce::jlimit (0.0, 100.0, (double) value); break;
        case kLength:      settings.length     = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        case kGrid:        settings.grid       = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        case kGate:        settings.gate       = juce::jlimit (5.0, 100.0, (double) value); break;
        case kPitch:       settings.pitch      = juce::jlimit (-12.0, 12.0, (double) value); break;
        case kDecay:       settings.decay      = juce::jlimit (0.0, 100.0, (double) value); break;
        case kFilter:      settings.filter     = juce::jlimit (-100.0, 100.0, (double) value); break;
        case kVariation:   settings.variation  = juce::jlimit (0.0, 100.0, (double) value); break;
        case kTape:        settings.tape       = juce::jlimit (10.0, 100.0, (double) value); break;
        case kMix:         settings.mix        = juce::jlimit (0.0, 100.0, (double) value); break;
        case kOutGain:     settings.outDb      = juce::jlimit (-24.0, 24.0, (double) value); break;
        case kPatternOn:   settings.patternOn  = value >= 0.5f; break;
        case kPattern:     settings.patternSel = juce::jlimit (0, pump::stutter::kUserPattern, (int) std::lround (value)); break;
        case kPatternStep: settings.patStep    = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        default:           return;
    }

    dirty = true;
}

void PumpStutterAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
