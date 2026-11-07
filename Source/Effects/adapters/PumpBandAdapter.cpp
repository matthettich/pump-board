#include "Effects/adapters/PumpBandAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpBandAdapter::params() const
{
    // pump's global layout, symbol for symbol (BandParams.hpp), then this adapter's per-band block.
    std::vector<ParamInfo> v = {
        { "out_gain",    "Output Gain",      -24.0f, 24.0f,   0.0f, "dB", false, 0.0f },
        { "mix",         "Mix",                0.0f, 100.0f, 100.0f, "%",  false, 0.0f },
        { "xover_mode",  "Crossover Mode",     0.0f, 1.0f,     0.0f, "",   false, 0.0f , pb::choices::names (pump::band::kXoverNames, 2) },
        { "bands",       "Bands",              1.0f, (float) pump::band::kNumBands, 3.0f, "", false, 0.0f },
        { "xover1",      "Crossover 1",       20.0f, 20000.0f, 120.0f, "Hz", false, 632.456f },
        { "xover2",      "Crossover 2",       20.0f, 20000.0f, 2000.0f, "Hz", false, 632.456f },
        { "xover3",      "Crossover 3",       20.0f, 20000.0f, 8000.0f, "Hz", false, 632.456f },
        { "lookahead",   "Lookahead",          0.0f, 10.0f,    0.0f, "ms", false, 0.0f },
        { "detection",   "Detection",          0.0f, 1.0f,     0.0f, "",   false, 0.0f , pb::choices::names (pump::band::kDetectNames, 2) },
        { "stereo_link", "Stereo Link",        0.0f, 100.0f, 100.0f, "%",  false, 0.0f },
        { "auto_makeup", "Auto Makeup",        0.0f, 1.0f,     0.0f, "",   true,  0.0f },
    };

    for (int b = 0; b < pump::band::kNumBands; ++b)
    {
        const juce::String n (b + 1);
        const auto& defaults = settings.band[b];

        v.push_back ({ "b" + n + "_threshold", "Band " + n + " Threshold", -60.0f, 0.0f, (float) defaults.thresh, "dB", false, 0.0f });
        v.push_back ({ "b" + n + "_ratio",     "Band " + n + " Ratio",       1.0f, 20.0f, (float) defaults.ratio, ":1", false, 4.472136f });
        v.push_back ({ "b" + n + "_attack",    "Band " + n + " Attack",      0.1f, 200.0f, (float) defaults.attack, "ms", false, 4.472136f });
        v.push_back ({ "b" + n + "_release",   "Band " + n + " Release",    10.0f, 2000.0f, (float) defaults.release, "ms", false, 141.421f });
        v.push_back ({ "b" + n + "_makeup",    "Band " + n + " Makeup",      0.0f, 24.0f, (float) defaults.makeup, "dB", false, 0.0f });
    }

    return v;
}

void PumpBandAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;
    prepared = true;
}

void PumpBandAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;
}

void PumpBandAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    if (index >= kBandParams && index < kNumParams)
    {
        const int offset = index - kBandParams;
        const int b = offset / kPerBandParams;
        const int field = offset % kPerBandParams;

        if (b < 0 || b >= pump::band::kNumBands)
            return;

        auto& band = settings.band[b];

        switch (field)
        {
            case 0: band.thresh  = juce::jlimit (-60.0f, 0.0f, value); break;
            case 1: band.ratio   = juce::jlimit (1.0f, 20.0f, value); break;
            case 2: band.attack  = juce::jlimit (0.1f, 200.0f, value); break;
            case 3: band.release = juce::jlimit (10.0f, 2000.0f, value); break;
            case 4: band.makeup  = juce::jlimit (0.0f, 24.0f, value); break;
            default: return;
        }

        dirty = true;
        return;
    }

    switch (index)
    {
        case kOutGain:    settings.outDb       = juce::jlimit (-24.0f, 24.0f, value); break;
        case kMix:        settings.mix         = juce::jlimit (0.0f, 100.0f, value); break;
        case kXoverMode:  settings.xoverMode   = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kBands:      settings.bands       = juce::jlimit (1, pump::band::kNumBands, (int) std::lround (value)); break;
        case kXover1:     settings.xover[0]    = juce::jlimit (20.0f, 20000.0f, value); break;
        case kXover2:     settings.xover[1]    = juce::jlimit (20.0f, 20000.0f, value); break;
        case kXover3:     settings.xover[2]    = juce::jlimit (20.0f, 20000.0f, value); break;
        case kLookahead:  settings.lookaheadMs = juce::jlimit (0.0f, 10.0f, value); break;
        case kDetection:  settings.detection   = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kStereoLink: settings.link        = juce::jlimit (0.0f, 100.0f, value); break;
        case kAutoMakeup: settings.autoMakeup  = value >= 0.5f; break;
        default:          return;
    }

    dirty = true;
}

void PumpBandAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
    {
        applySettingsTo (engine);
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
