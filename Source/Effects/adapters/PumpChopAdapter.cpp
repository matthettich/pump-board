#include "Effects/adapters/PumpChopAdapter.h"
#include "Effects/adapters/AssignHelpers.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpChopAdapter::params() const
{
    // pump's layout, symbol for symbol (ChopParams.hpp), minus bypass, the eight pad triggers and the MIDI switch.
    return {
        { "slices",   "Slices",        0.0f, 1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::chop::kSliceCountNames, 2) },   // 8 or 16
        { "length",   "Capture Length", 0.0f, (float) (pump::chop::kNumLengths - 1), (float) pump::chop::kOneBar, "", false, 0.0f , pb::choices::names (pump::chop::kLengthNames, 4) },
        { "quantise", "Quantise",      0.0f, (float) (pump::chop::kNumQuant - 1), (float) pump::chop::kQ16, "", false, 0.0f , pb::choices::names (pump::chop::kQuantNames, 5) },
        { "hold",     "Hold the Bar",  0.0f, 1.0f,    0.0f, "",   true,  0.0f },
        { "loop_two", "Two Pads Loop", 0.0f, 1.0f,    1.0f, "",   true,  0.0f },
        { "mix",      "Mix",           0.0f, 100.0f, 100.0f, "%",  false, 0.0f },
        { "out_gain", "Output Gain", -24.0f, 24.0f,    0.0f, "dB", false, 0.0f },
    };
}

void PumpChopAdapter::setTempo (double newBpm) noexcept
{
    settings.bpm = juce::jlimit (40.0, 240.0, newBpm);
    transport.bpm = settings.bpm;

    if (prepared)
    {
        engine.setTransport (transport);
        engine.setSettings (settings);
    }

    dirty = true;
}

void PumpChopAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    transport.bpm = settings.bpm;
    transport.playing = false;
    transport.valid = false;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
    prepared = true;
}

void PumpChopAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
}

void PumpChopAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kSlices:   assignField (settings.slices, juce::jlimit (0, 1, (int) std::lround (value)), dirty); break;
        case kLengthP:  assignField (settings.length, juce::jlimit (0, pump::chop::kNumLengths - 1, (int) std::lround (value)), dirty); break;
        case kQuantise: assignField (settings.quant, juce::jlimit (0, pump::chop::kNumQuant - 1, (int) std::lround (value)), dirty); break;
        case kHold:     assignField (settings.hold, value >= 0.5f, dirty); break;
        case kLoopTwo:  assignField (settings.loopTwo, value >= 0.5f, dirty); break;
        case kMix:      assignField (settings.mix, juce::jlimit (0.0, 100.0, (double) value) * 0.01, dirty); break;   // pump stores 0..1
        case kOutGain:  assignField (settings.outDb, juce::jlimit (-24.0, 24.0, (double) value), dirty); break;
        default:        return;
    }

}

void PumpChopAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        engine.setTransport (transport);    // the tempo travels with the transport as well as in the settings
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
