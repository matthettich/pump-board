#include "Effects/adapters/PumpEchoAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpEchoAdapter::params() const
{
    // pump's layout, symbol for symbol (EchoParams.hpp). Log-scaled params carry their geometric midpoint as
    // skewCentre, so a knob's centre lands on the same value pump's does. pump calls the routing `stereo`.
    return {
        { "mode",      "Mode",        0.0f,     4.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::echo::kModeNames, 5) },
        { "time_l",    "Time L",      1.0f,  4000.0f,  500.0f, "ms", false, std::sqrt (1.0f * 4000.0f) },
        { "time_r",    "Time R",      1.0f,  4000.0f,  750.0f, "ms", false, std::sqrt (1.0f * 4000.0f) },
        { "sync",      "Sync",        0.0f,     1.0f,    1.0f, "",   true,  0.0f },
        { "note_l",    "Note L",      0.0f,    17.0f,   11.0f, "",   false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },  // 1/4
        { "note_r",    "Note R",      0.0f,    17.0f,   12.0f, "",   false, 0.0f , pb::choices::names (pump::kNoteNames, 18) },  // 1/4D
        { "link",      "Link",        0.0f,     1.0f,    0.0f, "",   true,  0.0f },
        { "feedback",  "Feedback",    0.0f,   120.0f,   35.0f, "%",  false, 0.0f },
        { "stereo",    "Stereo",      0.0f,     2.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::echo::kRouteNames, 3) },
        { "width",     "Width",       0.0f,   100.0f,  100.0f, "%",  false, 0.0f },
        { "mod_depth", "Mod Depth",   0.0f,   100.0f,    0.0f, "%",  false, 0.0f },
        { "mod_rate",  "Mod Rate",   0.05f,    10.0f,    0.8f, "Hz", false, std::sqrt (0.05f * 10.0f) },
        { "age",       "Age",         0.0f,   100.0f,    0.0f, "%",  false, 0.0f },
        { "drive",     "Drive",       0.0f,    24.0f,    0.0f, "dB", false, 0.0f },
        { "low_cut",   "Low Cut",    10.0f,  2000.0f,   10.0f, "Hz", false, std::sqrt (10.0f * 2000.0f) },
        { "high_cut",  "High Cut", 1000.0f, 22000.0f, 22000.0f, "Hz", false, std::sqrt (1000.0f * 22000.0f) },
        { "diffuse",   "Diffuse",     0.0f,   100.0f,    0.0f, "%",  false, 0.0f },
        { "duck",      "Duck",        0.0f,   100.0f,    0.0f, "%",  false, 0.0f },
        { "freeze",    "Freeze",      0.0f,     1.0f,    0.0f, "",   true,  0.0f },
        { "pitch",     "Pitch",     -24.0f,    24.0f,   12.0f, "st", false, 0.0f },
        { "fine",      "Fine",     -100.0f,   100.0f,    0.0f, "",   false, 0.0f },
        { "pitch_mode", "Pitch Mode", 0.0f,     1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::echo::kShiftNames, 2) },
        { "mix",       "Mix",         0.0f,   100.0f,   30.0f, "%",  false, 0.0f },
        { "out_gain",  "Output Gain", -24.0f,  24.0f,    0.0f, "dB", false, 0.0f },
    };
}

void PumpEchoAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    engine.setTempo (bpm);
    engine.setSettings (settings);
    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpEchoAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise, so the adapter is in exactly the state pump's engine is in after its own prepare.
    engine.prepare (preparedRate, preparedBlock);
    engine.setTempo (bpm);
    engine.setSettings (settings);
    dirty = true;
}

void PumpEchoAdapter::setTempo (double beatsPerMinute) noexcept
{
    if (! std::isfinite (beatsPerMinute) || beatsPerMinute <= 0.0)
        return;

    bpm = beatsPerMinute;
    engine.setTempo (bpm);
}

void PumpEchoAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kMode:      settings.mode      = juce::jlimit (0, pump::echo::kNumModes - 1, (int) std::lround (value)); break;
        case kTimeL:     settings.timeL     = (double) value; break;
        case kTimeR:     settings.timeR     = (double) value; break;
        case kSync:      settings.sync      = value >= 0.5f; break;
        case kNoteL:     settings.noteL     = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        case kNoteR:     settings.noteR     = juce::jlimit (0, pump::kNumNotes - 1, (int) std::lround (value)); break;
        case kLink:      settings.link      = value >= 0.5f; break;
        case kFeedback:  settings.feedback  = (double) value; break;
        case kStereo:    settings.route     = juce::jlimit (0, pump::echo::kNumRoutes - 1, (int) std::lround (value)); break;
        case kWidth:     settings.width     = (double) value; break;
        case kModDepth:  settings.modDepth  = (double) value; break;
        case kModRate:   settings.modRate   = (double) value; break;
        case kAge:       settings.age       = (double) value; break;
        case kDrive:     settings.drive     = (double) value; break;
        case kLowCut:    settings.lowCut    = (double) value; break;
        case kHighCut:   settings.highCut   = (double) value; break;
        case kDiffuse:   settings.diffuse   = (double) value; break;
        case kDuck:      settings.duck      = (double) value; break;
        case kFreeze:    settings.freeze    = value >= 0.5f; break;
        case kPitch:     settings.pitch     = (double) value; break;
        case kFine:      settings.fine      = (double) value; break;
        case kPitchMode: settings.pitchMode = juce::jlimit (0, pump::echo::kNumShifts - 1, (int) std::lround (value)); break;
        case kMix:       settings.mix       = (double) value; break;
        case kOutGain:   settings.outDb     = (double) value; break;
        default:         return;
    }

    dirty = true;
}

void PumpEchoAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
