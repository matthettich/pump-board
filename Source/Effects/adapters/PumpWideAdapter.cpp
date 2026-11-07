#include "Effects/adapters/PumpWideAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpWideAdapter::params() const
{
    // pump's layout, symbol for symbol (WideParams.hpp), minus bypass and the hidden listen band. The per-band
    // width/stereoize pair is what pump itself generates, four times.
    std::vector<ParamInfo> v = {
        { "out_gain",   "Output Gain",  -24.0f, 24.0f,   0.0f, "dB", false, 0.0f },
        { "balance",    "Balance",     -100.0f, 100.0f,  0.0f, "%",  false, 0.0f },
        { "mono_bass",  "Mono Bass",      0.0f, 1.0f,    0.0f, "",   true,  0.0f },
        { "mono_freq",  "Mono Bass Frequency", 20.0f, 500.0f, 120.0f, "Hz", false, 100.0f },
        { "mono_slope", "Mono Bass Slope", 0.0f, 1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::wide::kSlopeNames, 2) },
        { "xover_mode", "Crossover Mode",  0.0f, 1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::wide::kXoverNames, 2) },
        { "bands",      "Bands",           1.0f, (float) pump::wide::kNumBands, (float) pump::wide::kNumBands, "", false, 0.0f },
        { "xover1",     "Crossover 1",    20.0f, 20000.0f, 120.0f, "Hz", false, 632.456f },
        { "xover2",     "Crossover 2",    20.0f, 20000.0f, 1000.0f, "Hz", false, 632.456f },
        { "xover3",     "Crossover 3",    20.0f, 20000.0f, 6000.0f, "Hz", false, 632.456f },
    };

    for (int b = 0; b < pump::wide::kNumBands; ++b)
    {
        const juce::String n (b + 1);
        v.push_back ({ "b" + n + "_width",     "Band " + n + " Width",     0.0f, 200.0f, 100.0f, "%", false, 0.0f });
        v.push_back ({ "b" + n + "_stereoize", "Band " + n + " Stereoize", 0.0f, 100.0f,   0.0f, "%", false, 0.0f });
    }

    return v;
}

void PumpWideAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
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

void PumpWideAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    applySettingsTo (engine);
    dirty = false;
}

void PumpWideAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    // the eight per-band controls, two per band: width then stereoize
    const int perBandBase = kBand1Width;
    const int perBandCount = pump::wide::kNumBands * 2;

    if (index >= perBandBase && index < perBandBase + perBandCount)
    {
        const int b = (index - perBandBase) / 2;
        const bool isWidth = ((index - perBandBase) % 2) == 0;

        if (b >= 0 && b < pump::wide::kNumBands)
        {
            if (isWidth)
                settings.band[b].width = juce::jlimit (0.0f, 200.0f, value);
            else
                settings.band[b].stereoize = juce::jlimit (0.0f, 100.0f, value);

            dirty = true;
        }

        return;
    }

    switch (index)
    {
        case kOutGain:   settings.outDb   = juce::jlimit (-24.0f, 24.0f, value); break;
        case kBalance:   settings.balance = juce::jlimit (-100.0f, 100.0f, value); break;
        case kMonoBass:  settings.monoBass = value >= 0.5f; break;
        case kMonoFreq:  settings.monoFreq = juce::jlimit (20.0f, 500.0f, value); break;
        case kMonoSlope: settings.monoSlope = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kXoverMode: settings.xoverMode = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kBands:     settings.bands   = juce::jlimit (1, pump::wide::kNumBands, (int) std::lround (value)); break;
        case kXover1:    settings.xover[0] = juce::jlimit (20.0f, 20000.0f, value); break;
        case kXover2:    settings.xover[1] = juce::jlimit (20.0f, 20000.0f, value); break;
        case kXover3:    settings.xover[2] = juce::jlimit (20.0f, 20000.0f, value); break;
        default:         return;
    }

    dirty = true;
}

void PumpWideAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
