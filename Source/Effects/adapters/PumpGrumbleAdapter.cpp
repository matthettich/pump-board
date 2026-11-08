#include "Effects/adapters/PumpGrumbleAdapter.h"
#include <cmath>
#include <cstring>

namespace pb
{
PumpGrumbleAdapter::PumpGrumbleAdapter() : engine (std::make_unique<pump::grumble::Engine>()) {}

std::vector<ParamInfo> PumpGrumbleAdapter::params() const
{
    // pump's GrumbleParams.hpp, symbol for symbol, minus bypass.
    return {
        { "beast_on",      "Beast On",      0.0f,    1.0f,   1.0f, "",   true,  0.0f },
        { "beast_pitch",   "Beast Pitch",  -24.0f,  12.0f,  -7.0f, "st", false, 0.0f },
        { "beast_formant", "Beast Formant", -12.0f, 12.0f,  -5.0f, "st", false, 0.0f },
        { "beast_fry",     "Beast Fry",     0.0f,  100.0f,  30.0f, "%",  false, 0.0f },
        { "beast_level",   "Beast Level",   0.0f,  100.0f,  80.0f, "%",  false, 0.0f },
        { "beast_pan",     "Beast Pan",  -100.0f,  100.0f,   0.0f, "%",  false, 0.0f },

        { "split_on",      "Split On",      0.0f,    1.0f,   0.0f, "",   true,  0.0f },
        { "split_pitch",   "Split Pitch",  -24.0f,  24.0f,   5.0f, "st", false, 0.0f },
        { "split_formant", "Split Formant", -12.0f, 12.0f,   4.0f, "st", false, 0.0f },
        { "split_wobble",  "Split Wobble",  0.0f,  100.0f,  25.0f, "%",  false, 0.0f },
        { "split_level",   "Split Level",   0.0f,  100.0f,  60.0f, "%",  false, 0.0f },
        { "split_pan",     "Split Pan",  -100.0f,  100.0f,   0.0f, "%",  false, 0.0f },

        { "growl_on",      "Growl On",      0.0f,    1.0f,   1.0f, "",   true,  0.0f },
        { "growl_pitch",   "Growl Pitch",  -24.0f,  12.0f, -12.0f, "st", false, 0.0f },
        { "growl_rough",   "Growl Rough",   0.0f,  100.0f,  55.0f, "%",  false, 0.0f },
        { "growl_breath",  "Growl Breath",  0.0f,  100.0f,  25.0f, "%",  false, 0.0f },
        { "growl_size",    "Growl Size",  -12.0f,  12.0f,  -4.0f, "st", false, 0.0f },
        { "growl_level",   "Growl Level",   0.0f,  100.0f,  60.0f, "%",  false, 0.0f },
        { "growl_pan",     "Growl Pan",  -100.0f,  100.0f,   0.0f, "%",  false, 0.0f },

        { "swarm_on",      "Swarm On",      0.0f,    1.0f,   0.0f, "",   true,  0.0f },
        { "swarm_pitch",   "Swarm Pitch",  -12.0f,  24.0f,   0.0f, "st", false, 0.0f },
        { "swarm_spread",  "Swarm Spread",  0.0f,   24.0f,   7.0f, "st", false, 0.0f },
        { "swarm_density", "Swarm Density", 0.0f,  100.0f,  50.0f, "%",  false, 0.0f },
        { "swarm_grain",   "Swarm Grain",   5.0f,  150.0f,  30.0f, "ms", false, std::sqrt (5.0f * 150.0f) },
        { "swarm_level",   "Swarm Level",   0.0f,  100.0f,  50.0f, "%",  false, 0.0f },
        { "swarm_width",   "Swarm Width",   0.0f,  100.0f,  70.0f, "%",  false, 0.0f },

        { "morph_x",       "Morph X",       0.0f,  100.0f,  50.0f, "%",  false, 0.0f },
        { "morph_y",       "Morph Y",       0.0f,  100.0f,  50.0f, "%",  false, 0.0f },

        { "gate",          "Gate",        -90.0f, -20.0f, -60.0f, "dB", false, 0.0f },     // -90 is "off"
        { "drive",         "Drive",         0.0f,   24.0f,   4.0f, "dB", false, 0.0f },
        { "tone",          "Tone",       -100.0f,  100.0f,   0.0f, "%",  false, 0.0f },
        { "cave",          "Cave",          0.0f,  100.0f,  12.0f, "%",  false, 0.0f },
        { "mix",           "Mix",           0.0f,  100.0f, 100.0f, "%",  false, 0.0f },
        { "out_gain",      "Output Gain", -24.0f,   24.0f,   0.0f, "dB", false, 0.0f },
    };
}

void PumpGrumbleAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine->set (settings);
    engine->prepare (preparedRate, preparedBlock);
    ++pushes;
    dirty = false;
    prepared = true;
}

void PumpGrumbleAdapter::reset()
{
    if (! prepared)
        return;

    engine->set (settings);
    engine->prepare (preparedRate, preparedBlock);   // a full re-initialise: the seeded noise starts over
    ++pushes;
    dirty = false;
}

void PumpGrumbleAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    using namespace pump::grumble;
    auto& s = settings;

    switch (index)
    {
        case kBeastOn:       assignBool  (s.on[kBeast], value); break;
        case kBeastPitch:    assignFloat (s.beastPitch, juce::jlimit (-24.0f, 12.0f, value)); break;
        case kBeastFormant:  assignFloat (s.beastFormant, juce::jlimit (-12.0f, 12.0f, value)); break;
        case kBeastFry:      assignFloat (s.beastFry, juce::jlimit (0.0f, 100.0f, value)); break;
        case kBeastLevel:    assignFloat (s.level[kBeast], juce::jlimit (0.0f, 100.0f, value)); break;
        case kBeastPan:      assignFloat (s.pan[kBeast], juce::jlimit (-100.0f, 100.0f, value)); break;

        case kSplitOn:       assignBool  (s.on[kSplit], value); break;
        case kSplitPitch:    assignFloat (s.splitPitch, juce::jlimit (-24.0f, 24.0f, value)); break;
        case kSplitFormant:  assignFloat (s.splitFormant, juce::jlimit (-12.0f, 12.0f, value)); break;
        case kSplitWobble:   assignFloat (s.splitWobble, juce::jlimit (0.0f, 100.0f, value)); break;
        case kSplitLevel:    assignFloat (s.level[kSplit], juce::jlimit (0.0f, 100.0f, value)); break;
        case kSplitPan:      assignFloat (s.pan[kSplit], juce::jlimit (-100.0f, 100.0f, value)); break;

        case kGrowlOn:       assignBool  (s.on[kGrowl], value); break;
        case kGrowlPitch:    assignFloat (s.growlPitch, juce::jlimit (-24.0f, 12.0f, value)); break;
        case kGrowlRough:    assignFloat (s.growlRough, juce::jlimit (0.0f, 100.0f, value)); break;
        case kGrowlBreath:   assignFloat (s.growlBreath, juce::jlimit (0.0f, 100.0f, value)); break;
        case kGrowlSize:     assignFloat (s.growlSize, juce::jlimit (-12.0f, 12.0f, value)); break;
        case kGrowlLevel:    assignFloat (s.level[kGrowl], juce::jlimit (0.0f, 100.0f, value)); break;
        case kGrowlPan:      assignFloat (s.pan[kGrowl], juce::jlimit (-100.0f, 100.0f, value)); break;

        case kSwarmOn:       assignBool  (s.on[kSwarm], value); break;
        case kSwarmPitch:    assignFloat (s.swarmPitch, juce::jlimit (-12.0f, 24.0f, value)); break;
        case kSwarmSpread:   assignFloat (s.swarmSpread, juce::jlimit (0.0f, 24.0f, value)); break;
        case kSwarmDensity:  assignFloat (s.swarmDensity, juce::jlimit (0.0f, 100.0f, value)); break;
        case kSwarmGrain:    assignFloat (s.swarmGrainMs, juce::jlimit (5.0f, 150.0f, value)); break;
        case kSwarmLevel:    assignFloat (s.level[kSwarm], juce::jlimit (0.0f, 100.0f, value)); break;
        case kSwarmWidth:    assignFloat (s.pan[kSwarm], juce::jlimit (0.0f, 100.0f, value)); break;   // SWARM's "pan" is its width

        case kMorphX:        assignFloat (s.morphX, juce::jlimit (0.0f, 100.0f, value)); break;
        case kMorphY:        assignFloat (s.morphY, juce::jlimit (0.0f, 100.0f, value)); break;
        case kGate:          assignFloat (s.gateDb, juce::jlimit (kGateOff, -20.0f, value)); break;
        case kDrive:         assignFloat (s.drive, juce::jlimit (0.0f, 24.0f, value)); break;
        case kTone:          assignFloat (s.tone, juce::jlimit (-100.0f, 100.0f, value)); break;
        case kCave:          assignFloat (s.cave, juce::jlimit (0.0f, 100.0f, value)); break;
        case kMix:           assignFloat (s.mix, juce::jlimit (0.0f, 100.0f, value)); break;
        case kOutGain:       assignFloat (s.outDb, juce::jlimit (-24.0f, 24.0f, value)); break;
        default:             return;
    }
}

void PumpGrumbleAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if ((size_t) numSamples > rightScratch.size())
        return;

    if (dirty)
    {
        engine->set (settings);
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

    engine->process (left, right, numSamples);   // stereo in place; the voice is the mono sum

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
