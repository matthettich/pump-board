#include "Effects/adapters/PumpTuneAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
PumpTuneAdapter::PumpTuneAdapter() : engine (std::make_unique<pump::tune::Engine>())
{
    settings.mode = pump::tune::kLive;
    settings.midi = pump::tune::kMidiOff;
    settings.range = pump::tune::kRangeMid;
}

std::vector<ParamInfo> PumpTuneAdapter::params() const
{
    // pump's TuneParams.hpp, symbol for symbol, minus mode / midi / range / bypass (see the header).
    return {
        { "key",            "Key",            0.0f,   11.0f,   0.0f, "",   false, 0.0f , pb::choices::names (pump::pitch::kKeyNames, 12) },
        { "scale",          "Scale",          0.0f, (float) (pump::pitch::kNumScales - 1), (float) pump::pitch::kChromatic, "", false, 0.0f ,
                                              pb::choices::names (pump::pitch::kScaleNames, pump::pitch::kNumScales) },
        { "retune",         "Retune Speed",   0.0f,  400.0f,  20.0f, "ms", false, 100.0f },   // pump: power 2, centre = a quarter
        { "amount",         "Amount",         0.0f,  100.0f, 100.0f, "%",  false, 0.0f },
        { "humanize",       "Humanize",       0.0f,  100.0f,   0.0f, "%",  false, 0.0f },
        { "flex",           "Flex",           0.0f,  100.0f,   0.0f, "",   false, 0.0f },
        { "vib_depth",      "Vibrato Depth",  0.0f,  100.0f,   0.0f, "",   false, 0.0f },
        { "vib_rate",       "Vibrato Rate",   2.0f,    9.0f,   5.5f, "Hz", false, 0.0f },
        { "transpose",      "Transpose",    -24.0f,   24.0f,   0.0f, "st", false, 0.0f },
        { "formant",        "Formant",      -12.0f,   12.0f,   0.0f, "st", false, 0.0f },
        { "formant_follow", "Formants Follow Pitch", 0.0f, 1.0f, 0.0f, "", true,  0.0f },
        { "harm_mode",      "Harmony",        0.0f,    2.0f,   0.0f, "",   false, 0.0f , pb::choices::names (pump::tune::kHarmNames, 3) },
        { "h1",             "Harmony 1",    -12.0f,   12.0f,   2.0f, "",   false, 0.0f },
        { "h2",             "Harmony 2",    -12.0f,   12.0f,  -3.0f, "",   false, 0.0f },
        { "h3",             "Harmony 3",    -12.0f,   12.0f,   4.0f, "",   false, 0.0f },
        { "h4",             "Harmony 4",    -12.0f,   12.0f,   7.0f, "",   false, 0.0f },
        { "h1_level",       "Harmony 1 Level", 0.0f, 100.0f,  70.0f, "%",  false, 0.0f },
        { "h2_level",       "Harmony 2 Level", 0.0f, 100.0f,  60.0f, "%",  false, 0.0f },
        { "h3_level",       "Harmony 3 Level", 0.0f, 100.0f,   0.0f, "%",  false, 0.0f },
        { "h4_level",       "Harmony 4 Level", 0.0f, 100.0f,   0.0f, "%",  false, 0.0f },
        { "h1_pan",         "Harmony 1 Pan", -100.0f, 100.0f, -45.0f, "",  false, 0.0f },
        { "h2_pan",         "Harmony 2 Pan", -100.0f, 100.0f,  45.0f, "",  false, 0.0f },
        { "h3_pan",         "Harmony 3 Pan", -100.0f, 100.0f, -80.0f, "",  false, 0.0f },
        { "h4_pan",         "Harmony 4 Pan", -100.0f, 100.0f,  80.0f, "",  false, 0.0f },
        { "harm_formant",   "Harmony Formant", -12.0f, 12.0f,  0.0f, "st", false, 0.0f },
        { "harm_humanize",  "Harmony Humanize", 0.0f, 100.0f, 30.0f, "%",  false, 0.0f },
        { "lead",           "Lead Level",     0.0f,  100.0f, 100.0f, "%",  false, 0.0f },
        { "harm_level",     "Harmony Level", -24.0f,  12.0f,   0.0f, "dB", false, 0.0f },
        { "out_gain",       "Output Gain",  -24.0f,   24.0f,   0.0f, "dB", false, 0.0f },
    };
}

void PumpTuneAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine->setSettings (settings);      // TUNE applies a settings struct at once; range is fixed, so no rebuild
    engine->prepare (preparedRate, preparedBlock);
    ++pushes;
    dirty = false;
    prepared = true;
}

void PumpTuneAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise, as CRUSH does: the humanize drift generators are seeded in prepare() and keep running,
    // so only a fresh prepare puts the adapter in the state pump's engine is in after its own prepare.
    engine->setSettings (settings);
    engine->prepare (preparedRate, preparedBlock);
    ++pushes;
    dirty = false;
}

void PumpTuneAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kKey:          assignInt (settings.key, juce::jlimit (0, 11, (int) std::lround (value))); break;
        case kScale:        assignInt (settings.scale, juce::jlimit (0, pump::pitch::kNumScales - 1, (int) std::lround (value))); break;
        case kRetune:       assignDouble (settings.retuneMs, juce::jlimit (0.0f, 400.0f, value)); break;
        case kAmount:       assignDouble (settings.amount, juce::jlimit (0.0f, 100.0f, value)); break;
        case kHumanize:     assignDouble (settings.humanize, juce::jlimit (0.0f, 100.0f, value)); break;
        case kFlex:         assignDouble (settings.flexCents, juce::jlimit (0.0f, 100.0f, value)); break;
        case kVibDepth:     assignDouble (settings.vibDepth, juce::jlimit (0.0f, 100.0f, value)); break;
        case kVibRate:      assignDouble (settings.vibRate, juce::jlimit (2.0f, 9.0f, value)); break;
        case kTranspose:    assignInt (settings.transpose, juce::jlimit (-24, 24, (int) std::lround (value))); break;
        case kFormant:      assignDouble (settings.formant, juce::jlimit (-12.0f, 12.0f, value)); break;
        case kFormantFollow:
        {
            const bool follow = value >= 0.5f;

            if (settings.formantFollow != follow)
            {
                settings.formantFollow = follow;
                dirty = true;
            }

            break;
        }
        case kHarmMode:     assignInt (settings.harmMode, juce::jlimit (0, pump::tune::kNumHarm - 1, (int) std::lround (value))); break;
        case kInterval0: case kInterval1: case kInterval2: case kInterval3:
            assignInt (settings.interval[index - kInterval0], juce::jlimit (-12, 12, (int) std::lround (value))); break;
        case kLevel0: case kLevel1: case kLevel2: case kLevel3:
            assignDouble (settings.level[index - kLevel0], juce::jlimit (0.0f, 100.0f, value)); break;
        case kPan0: case kPan1: case kPan2: case kPan3:
            assignDouble (settings.pan[index - kPan0], juce::jlimit (-100.0f, 100.0f, value)); break;
        case kHarmFormant:  assignDouble (settings.harmFormant, juce::jlimit (-12.0f, 12.0f, value)); break;
        case kHarmHumanize: assignDouble (settings.harmHumanize, juce::jlimit (0.0f, 100.0f, value)); break;
        case kLead:         assignDouble (settings.lead, juce::jlimit (0.0f, 100.0f, value)); break;
        case kHarmLevel:    assignDouble (settings.harmLevelDb, juce::jlimit (-24.0f, 12.0f, value)); break;
        case kOutGain:      assignDouble (settings.outDb, juce::jlimit (-24.0f, 24.0f, value)); break;
        default:            return;
    }
}

void PumpTuneAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if ((size_t) numSamples > rightScratch.size())
        return;                          // never take a block bigger than prepare() promised

    if (dirty)
    {
        engine->setSettings (settings);  // block start
        ++pushes;
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
        std::memcpy (rightScratch.data(), left, (size_t) numSamples * sizeof (float));
        right = rightScratch.data();
    }

    // In place: the engine reads both inputs of a frame before it writes that frame's outputs. No MIDI events.
    engine->process (left, right, left, right, numSamples, nullptr, 0);

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
