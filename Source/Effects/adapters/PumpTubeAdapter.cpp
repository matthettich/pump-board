#include "Effects/adapters/PumpTubeAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpTubeAdapter::params() const
{
    // pump's layout, symbol for symbol (TubeParams.hpp). Log-scaled filters carry their geometric midpoint.
    return {
        { "mode",         "Circuit",     0.0f,     5.0f,    1.0f, "",   false, 0.0f , pb::choices::names (pump::tube::kModeNames, 6) },   // Triode
        { "drive",        "Drive",       0.0f,    36.0f,   12.0f, "dB", false, 0.0f },
        { "bias",         "Bias",     -100.0f,   100.0f,    0.0f, "%",  false, 0.0f },
        { "sag",          "Sag",         0.0f,   100.0f,   25.0f, "%",  false, 0.0f },
        { "voltage",      "Voltage",     0.0f,   100.0f,  100.0f, "%",  false, 0.0f },
        { "stages",       "Stages",      1.0f,     3.0f,    1.0f, "",   false, 0.0f , pb::choices::names (pump::tube::kStageNames, 3) },
        { "focus",        "Focus",       0.0f,     3.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::tube::kFocusNames, 4) },
        { "low_cut",      "Low Cut",    10.0f,  1000.0f,   10.0f, "Hz", false, std::sqrt (10.0f * 1000.0f) },
        { "tone",         "Tone",     -100.0f,   100.0f,    0.0f, "%",  false, 0.0f },
        { "high_cut",     "High Cut", 1000.0f, 22000.0f, 22000.0f, "Hz", false, std::sqrt (1000.0f * 22000.0f) },
        { "mix",          "Mix",         0.0f,   100.0f,  100.0f, "%",  false, 0.0f },
        { "out_gain",     "Output Gain", -24.0f,  24.0f,    0.0f, "dB", false, 0.0f },
        { "auto_gain",    "Auto Gain",   0.0f,     1.0f,    1.0f, "",   true,  0.0f },
        { "oversampling", "Oversampling", 0.0f,    3.0f,    2.0f, "",   false, 0.0f , pb::choices::names (pump::tube::kOsNames, 4) },   // 4x
    };
}

void PumpTubeAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    pendingOversampling = -1;           // prepare takes the stored settings as they are
    silentBlocks = 0;

    // The engine switches oversampling with a fade over its next blocks, so latency() reports the old figure until
    // audio has actually run. Prime it here, in silence, so latencySamples() is right from the first block: the
    // chain reads it for delay compensation before any audio flows (card 5.33).
    leftScratch.assign ((size_t) preparedBlock, 0.0f);

    for (double primed = 0.0; primed < 0.05 * preparedRate; primed += (double) preparedBlock)
    {
        std::fill (leftScratch.begin(), leftScratch.end(), 0.0f);
        std::fill (rightScratch.begin(), rightScratch.end(), 0.0f);
        engine.process (leftScratch.data(), rightScratch.data(), preparedBlock);
    }

    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpTubeAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise, so the adapter is in exactly the state pump's engine is in after its own prepare.
    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    pendingOversampling = -1;
    silentBlocks = 0;
    dirty = true;
}

void PumpTubeAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kMode:         settings.mode         = juce::jlimit (0, pump::tube::kNumModes - 1, (int) std::lround (value)); break;
        case kDrive:        settings.drive        = (double) value; break;
        case kBias:         settings.bias         = (double) value; break;
        case kSag:          settings.sag          = (double) value; break;
        case kVoltage:      settings.voltage      = (double) value; break;
        case kStages:       settings.stages       = juce::jlimit (1, 3, (int) std::lround (value)); break;
        case kFocus:        settings.focus        = juce::jlimit (0, pump::tube::kNumFocus - 1, (int) std::lround (value)); break;
        case kLowCut:       settings.lowCut       = (double) value; break;
        case kTone:         settings.tone         = (double) value; break;
        case kHighCut:      settings.highCut      = (double) value; break;
        case kMix:          settings.mix          = (double) value; break;
        case kOutGain:      settings.outDb        = (double) value; break;
        case kAutoGain:     settings.autoGain     = value >= 0.5f; break;
        case kOversampling:
        {
            const int requested = juce::jlimit (0, 3, (int) std::lround (value));
            settings.oversampling = requested;          // the choice is kept either way, so a save remembers it

            if (prepared && requested != engine.oversampling())
                pendingOversampling = requested;        // into force at the next silence
            else
                dirty = true;

            return;                                     // not an ordinary dirty flag: see below
        }
        default:            return;
    }

    dirty = true;
}

void PumpTubeAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    // Silence is measured before anything is processed, so the switch happens at the start of a quiet block.
    const float inputPeak = buffer.getMagnitude (0, numSamples);

    if (inputPeak < 1.0e-5f)
        ++silentBlocks;
    else
        silentBlocks = 0;

    // Card 5.33: OVERSAMPLING waits for silence. The engine fades its own output across the switch, so this is about
    // the pedal's latency jumping mid-note, not about clicks.
    if (pendingOversampling >= 0 && silentBlocks >= 2)
    {
        settings.oversampling = pendingOversampling;
        pendingOversampling = -1;
        dirty = true;
    }

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
