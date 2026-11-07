#include "Effects/adapters/PumpVerbAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpVerbAdapter::params() const
{
    // pump's layout, symbol for symbol (VerbParams.hpp). skewCentre is sqrt(min*max) wherever pump scales the
    // knob logarithmically, and 0 (linear) everywhere else.
    return {
        { "mode",         "Mode",              0.0f,             (float) (pump::verb::kNumModes - 1), 0.0f,                   "",   false, 0.0f , pb::choices::names (pump::verb::kModeNames, pump::verb::kNumModes) },
        { "predelay",     "Pre-Delay",         0.0f,             (float) pump::verb::kMaxPredelayMs,  20.0f,                  "ms", false, 0.0f },
        { "predelay_sync","Pre-Delay Sync",    0.0f,             1.0f,                                0.0f,                   "",   true,  0.0f },
        { "predelay_note","Pre-Delay Note",    0.0f,             (float) pump::verb::kMaxSyncNote,    (float) pump::kNote32, "", false, 0.0f },
        { "size",         "Size",              0.0f,             100.0f,                              50.0f,                  "%",  false, 0.0f },
        { "decay",        "Decay",             0.1f,             30.0f,                               2.2f,                   "s",  false, 1.7320508f },
        { "diffusion",    "Diffusion",         0.0f,             100.0f,                              80.0f,                  "%",  false, 0.0f },
        { "early_late",   "Early / Late",     -100.0f,           100.0f,                              0.0f,                   "%",  false, 0.0f },
        { "bass_mult",    "Bass Multiplier",   0.5f,             2.0f,                                1.2f,                   "",   false, 1.0f },
        { "crossover",    "Crossover",         50.0f,            2000.0f,                             600.0f,                 "Hz", false, 316.22775f },
        { "high_damp",    "High Damping",      0.1f,             1.0f,                                0.55f,                  "",   false, 0.0f },
        { "damp_freq",    "Damping Frequency", 1000.0f,          20000.0f,                            6000.0f,                "Hz", false, 4472.136f },
        { "low_cut",      "Low Cut",           pump::verb::kLowCutOff,  1000.0f,                     pump::verb::kLowCutOff, "Hz", false, 100.0f },
        { "high_cut",     "High Cut",          1000.0f,          pump::verb::kHighCutOff,             pump::verb::kHighCutOff, "Hz", false, 4690.416f },
        { "mod_depth",    "Modulation Depth",  0.0f,             100.0f,                              40.0f,                  "%",  false, 0.0f },
        { "mod_rate",     "Modulation Rate",   0.05f,            5.0f,                                0.7f,                   "Hz", false, 0.5f },
        { "width",        "Width",             0.0f,             150.0f,                              100.0f,                 "%",  false, 0.0f },
        { "age",          "Age",               0.0f,             100.0f,                              35.0f,                  "%",  false, 0.0f },
        { "env_time",     "Gate / Reverse Time", 50.0f,          1500.0f,                             320.0f,                 "ms", false, 273.86f },
        { "shimmer_pitch","Shimmer Pitch",   -24.0f,             24.0f,                               12.0f,                  "st", false, 0.0f },
        { "shimmer_amount","Shimmer Amount",   0.0f,             100.0f,                              50.0f,                  "%",  false, 0.0f },
        { "duck",         "Duck",              0.0f,             100.0f,                              0.0f,                   "%",  false, 0.0f },
        { "freeze",       "Freeze",            0.0f,             1.0f,                                0.0f,                   "",   true,  0.0f },
        { "mix",          "Mix",               0.0f,             100.0f,                              30.0f,                  "%",  false, 0.0f },
        { "out_gain",     "Output Gain",     -24.0f,             24.0f,                               0.0f,                   "dB", false, 0.0f },
    };
}

void PumpVerbAdapter::setTempo (double newBpm) noexcept
{
    bpm = juce::jlimit (20.0, 999.0, newBpm);

    if (prepared)
        engine.setTempo (bpm);
}

void PumpVerbAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    engine.setTempo (bpm);
    engine.setSettings (settings);      // pump's plugin hands its settings over right after prepare
    dirty = true;                       // and leaves one change pending
    prepared = true;
}

void PumpVerbAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setTempo (bpm);
    engine.setSettings (settings);
    dirty = true;
}

void PumpVerbAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kMode:        settings.mode      = juce::jlimit (0, pump::verb::kNumModes - 1, (int) std::lround (value)); break;
        case kPredelay:    settings.predelayMs = juce::jlimit (0.0, (double) pump::verb::kMaxPredelayMs, (double) value); break;
        case kSync:        settings.sync      = value >= 0.5f; break;
        case kNote:        settings.note      = juce::jlimit (0, pump::verb::kMaxSyncNote, (int) std::lround (value)); break;
        case kSize:        settings.size      = juce::jlimit (0.0, 100.0, (double) value); break;
        case kDecay:       settings.decay     = juce::jlimit (0.1, 30.0, (double) value); break;
        case kDiffusion:   settings.diffusion = juce::jlimit (0.0, 100.0, (double) value); break;
        case kBalance:     settings.balance   = juce::jlimit (-100.0, 100.0, (double) value); break;
        case kBassMult:    settings.bassMult  = juce::jlimit (0.5, 2.0, (double) value); break;
        case kCrossover:   settings.crossover = juce::jlimit (50.0, 2000.0, (double) value); break;
        case kHighDamp:    settings.highDamp  = juce::jlimit (0.1, 1.0, (double) value); break;
        case kDampFreq:    settings.dampFreq  = juce::jlimit (1000.0, 20000.0, (double) value); break;
        case kLowCut:      settings.lowCut    = juce::jlimit ((double) pump::verb::kLowCutOff, 1000.0, (double) value); break;
        case kHighCut:     settings.highCut   = juce::jlimit (1000.0, (double) pump::verb::kHighCutOff, (double) value); break;
        case kModDepth:    settings.modDepth  = juce::jlimit (0.0, 100.0, (double) value); break;
        case kModRate:     settings.modRate   = juce::jlimit (0.05, 5.0, (double) value); break;
        case kWidth:       settings.width     = juce::jlimit (0.0, 150.0, (double) value); break;
        case kAge:         settings.age       = juce::jlimit (0.0, 100.0, (double) value); break;
        case kEnvTime:     settings.envTimeMs = juce::jlimit (50.0, 1500.0, (double) value); break;
        case kPitch:       settings.pitch     = juce::jlimit (-24, 24, (int) std::lround (value)); break;
        case kShimmer:     settings.shimmer   = juce::jlimit (0.0, 100.0, (double) value); break;
        case kDuck:        settings.duck      = juce::jlimit (0.0, 100.0, (double) value); break;
        case kFreeze:      settings.freeze    = value >= 0.5f; break;
        case kMix:         settings.mix       = juce::jlimit (0.0, 100.0, (double) value); break;
        case kOutGain:     settings.outDb     = juce::jlimit (-24.0, 24.0, (double) value); break;
        default:           return;
    }

    dirty = true;
}

void PumpVerbAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        engine.setSettings (settings);      // block start, as pump's engine expects
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
            return;                          // never take a block bigger than prepare() promised

        std::memcpy (rightScratch.data(), left, (size_t) numSamples * sizeof (float));
        right = rightScratch.data();
    }

    engine.process (left, right, numSamples);

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
