#include "Effects/adapters/PumpGrainAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpGrainAdapter::params() const
{
    // pump's layout, symbol for symbol (GrainParams.hpp), minus bypass.
    return {
        { "position",     "Position",      0.0f, 4000.0f,   250.0f, "ms", false, 0.0f },
        { "size",         "Grain Size",    5.0f, 1000.0f,   120.0f, "ms", false, 70.711f },
        { "density",      "Density",       1.0f, 100.0f,     12.0f, "",   false, 10.0f },
        { "spray",        "Spray",         0.0f, 100.0f,     10.0f, "%",  false, 0.0f },
        { "sync",         "Tempo Sync",    0.0f, 1.0f,        0.0f, "",   true,  0.0f },
        { "division",     "Sync Division", 0.0f, (float) (pump::kNumNotes - 1), (float) pump::kNote16, "", false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },
        { "envelope",     "Envelope",      0.0f, (float) (pump::grain::kNumEnvelopes - 1), (float) pump::grain::kEnvSmooth, "", false, 0.0f , pb::choices::names (pump::grain::kEnvelopeNames, 5) },
        { "reverse",      "Reverse",       0.0f, 100.0f,      0.0f, "%",  false, 0.0f },
        { "stereo",       "Stereo Spread", 0.0f, 100.0f,     40.0f, "%",  false, 0.0f },
        { "pitch",        "Pitch",       -24.0f, 24.0f,       0.0f, "st", false, 0.0f },
        { "pitch_spread", "Pitch Spread",  0.0f, 24.0f,       0.0f, "st", false, 0.0f },
        { "quantize",     "Quantize",      0.0f, (float) (pump::grain::kNumScales - 1), (float) pump::grain::kScaleOff, "", false, 0.0f , pb::choices::names (pump::grain::kScaleNames, 7) },
        { "feedback",     "Feedback",      0.0f, 100.0f,      0.0f, "%",  false, 0.0f },
        { "filter",       "Filter",        0.0f, (float) (pump::grain::kNumFilterModes - 1), (float) pump::grain::kFilterOff, "", false, 0.0f , pb::choices::names (pump::grain::kFilterNames, 4) },
        { "cutoff",       "Cutoff",       20.0f, 20000.0f,  2000.0f, "Hz", false, 632.456f },
        { "resonance",    "Resonance",     0.0f, 100.0f,     20.0f, "%",  false, 0.0f },
        { "space",        "Space",         0.0f, 100.0f,     25.0f, "%",  false, 0.0f },
        { "motion_rate",  "Motion Rate",   0.02f, 10.0f,      0.25f, "Hz", false, 0.447214f },
        { "motion_depth", "Motion Depth",  0.0f, 100.0f,     20.0f, "%",  false, 0.0f },
        { "morph_x",      "Morph X",     -100.0f, 100.0f,     0.0f, "%",  false, 0.0f },
        { "morph_y",      "Morph Y",     -100.0f, 100.0f,     0.0f, "%",  false, 0.0f },
        { "freeze",       "Freeze",        0.0f, 1.0f,        0.0f, "",   true,  0.0f },
        { "mix",          "Mix",           0.0f, 100.0f,     50.0f, "%",  false, 0.0f },
        { "out_gain",     "Output Gain", -24.0f, 24.0f,       0.0f, "dB", false, 0.0f },
    };
}

void PumpGrainAdapter::setTempo (double newBpm) noexcept
{
    bpm = juce::jlimit (20.0, 999.0, newBpm);
    transport.bpm = bpm;

    if (prepared)
        engine.setTransport (transport);
}

void PumpGrainAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
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

void PumpGrainAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTransport (transport);
    engine.setSettings (settings);
    dirty = true;
}

void PumpGrainAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kPosition:    settings.positionMs    = juce::jlimit (0.0, 4000.0, (double) value); break;
        case kSize:        settings.sizeMs        = juce::jlimit (5.0, 1000.0, (double) value); break;
        case kDensity:     settings.density       = juce::jlimit (1.0, 100.0, (double) value); break;
        case kSpray:       settings.spray         = juce::jlimit (0.0, 100.0, (double) value); break;
        case kSync:        settings.sync          = value >= 0.5f; break;
        case kDivision:    settings.division      = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        case kEnvelope:    settings.envelope      = juce::jlimit (0, pump::grain::kNumEnvelopes - 1, (int) std::lround (value)); break;
        case kReverse:     settings.reverse       = juce::jlimit (0.0, 100.0, (double) value); break;
        case kStereo:      settings.stereo        = juce::jlimit (0.0, 100.0, (double) value); break;
        case kPitch:       settings.pitch         = juce::jlimit (-24.0, 24.0, (double) value); break;
        case kPitchSpread: settings.pitchSpread   = juce::jlimit (0.0, 24.0, (double) value); break;
        case kQuantize:    settings.quantize      = juce::jlimit (0, pump::grain::kNumScales - 1, (int) std::lround (value)); break;
        case kFeedback:    settings.feedback      = juce::jlimit (0.0, 100.0, (double) value); break;
        case kFilter:      settings.filter        = juce::jlimit (0, pump::grain::kNumFilterModes - 1, (int) std::lround (value)); break;
        case kCutoff:      settings.cutoff        = juce::jlimit (20.0, 20000.0, (double) value); break;
        case kResonance:   settings.resonance     = juce::jlimit (0.0, 100.0, (double) value); break;
        case kSpace:       settings.space         = juce::jlimit (0.0, 100.0, (double) value); break;
        case kMotionRate:  settings.motionRate    = juce::jlimit (0.02, 10.0, (double) value); break;
        case kMotionDepth: settings.motionDepth   = juce::jlimit (0.0, 100.0, (double) value); break;
        case kMorphX:      settings.morphX        = juce::jlimit (-100.0, 100.0, (double) value); break;
        case kMorphY:      settings.morphY        = juce::jlimit (-100.0, 100.0, (double) value); break;
        case kFreeze:      settings.freeze        = value >= 0.5f; break;
        case kMix:         settings.mix           = juce::jlimit (0.0, 100.0, (double) value); break;
        case kOutGain:     settings.outDb         = juce::jlimit (-24.0, 24.0, (double) value); break;
        default:           return;
    }

    dirty = true;
}

void PumpGrainAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
