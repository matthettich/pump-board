#include "Effects/adapters/PumpHeatAdapter.h"
#include "Effects/adapters/AssignHelpers.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpHeatAdapter::params() const
{
    // pump's layout, global block first, then four bands of seven (HeatParams.hpp). Log-scaled params carry the
    // geometric midpoint as skewCentre, so a knob's centre lands on the same value pump's does.
    std::vector<ParamInfo> list {
        { "out_gain",     "Output Gain",    -24.0f,   24.0f,    0.0f, "dB", false, 0.0f },
        { "mix",          "Mix",              0.0f,  100.0f,  100.0f, "%",  false, 0.0f },
        { "oversampling", "Oversampling",     0.0f,    3.0f,    2.0f, "",   false, 0.0f , pb::choices::names (pump::heat::kOsNames, 4) },
        { "auto_gain",    "Auto Gain",        0.0f,    1.0f,    1.0f, "",   true,  0.0f },
        { "xover_mode",   "Crossover Mode",   0.0f,    1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::heat::kXoverNames, 2) },
        { "bands",        "Bands",            1.0f,    4.0f,    3.0f, "",   false, 0.0f },
        { "xover1",       "Crossover 1",     20.0f, 20000.0f,  150.0f, "Hz", false, std::sqrt (20.0f * 20000.0f) },
        { "xover2",       "Crossover 2",     20.0f, 20000.0f, 3000.0f, "Hz", false, std::sqrt (20.0f * 20000.0f) },
        { "xover3",       "Crossover 3",     20.0f, 20000.0f, 9000.0f, "Hz", false, std::sqrt (20.0f * 20000.0f) },
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        const juce::String n = juce::String (b + 1), p = "b" + n + "_", N = "Band " + n + " ";

        list.push_back ({ p + "mode",   N + "Mode",   0.0f,   5.0f,   0.0f, "",   false, 0.0f });
        list.push_back ({ p + "drive",  N + "Drive",  0.0f,  24.0f,   6.0f, "dB", false, 0.0f });
        list.push_back ({ p + "mix",    N + "Mix",    0.0f, 100.0f, 100.0f, "%",  false, 0.0f });
        list.push_back ({ p + "level",  N + "Level", -12.0f,  12.0f,   0.0f, "dB", false, 0.0f });
        list.push_back ({ p + "solo",   N + "Solo",   0.0f,   1.0f,   0.0f, "",   true,  0.0f });
        list.push_back ({ p + "mute",   N + "Mute",   0.0f,   1.0f,   0.0f, "",   true,  0.0f });
        list.push_back ({ p + "bypass", N + "Bypass", 0.0f,   1.0f,   0.0f, "",   true,  0.0f });
    }

    return list;
}

void PumpHeatAdapter::applySettings() noexcept
{
    engine.setBands (bandCountValue);
    engine.setCrossovers (xover[0], xover[1], xover[2]);
    engine.setXoverMode (xoverModeValue);
    engine.setOversampling (osIndex);
    engine.setAutoGain (autoGainOnValue);
    engine.setMix (mixValue);
    engine.setOutput (outDbValue);

    for (int b = 0; b < kNumBands; ++b)
        engine.setBand (b, bands[(size_t) b]);

    dirty = false;
}

void PumpHeatAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    // pump's engine builds its structure in prepare(), so prepare first and hand it the settings after — the
    // null test makes exactly these calls in exactly this order on pump's engine.
    engine.prepare (preparedRate, preparedBlock);
    applySettings();
    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpHeatAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    applySettings();
    dirty = true;
}

void PumpHeatAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    if (index == kOutGain)      { assignField (outDbValue, juce::jlimit (-24.0f, 24.0f, value), dirty); return; }
    if (index == kMix)          { assignField (mixValue, juce::jlimit (0.0f, 100.0f, value), dirty); return; }
    if (index == kOversampling) { assignField (osIndex, juce::jlimit (0, 3, (int) std::lround (value)), dirty); return; }
    if (index == kAutoGain)     { assignField (autoGainOnValue, value >= 0.5f, dirty); return; }
    if (index == kXoverMode)    { assignField (xoverModeValue, juce::jlimit (0, 1, (int) std::lround (value)), dirty); return; }
    if (index == kBands)        { assignField (bandCountValue, juce::jlimit (1, kNumBands, (int) std::lround (value)), dirty); return; }
    if (index == kXover1)       { assignField (xover[0], juce::jlimit (20.0f, 20000.0f, value), dirty); return; }
    if (index == kXover2)       { assignField (xover[1], juce::jlimit (20.0f, 20000.0f, value), dirty); return; }
    if (index == kXover3)       { assignField (xover[2], juce::jlimit (20.0f, 20000.0f, value), dirty); return; }

    const int bandIndex = (index - kFirstBand) / kBandFields;
    const int field = (index - kFirstBand) % kBandFields;

    if (bandIndex < 0 || bandIndex >= kNumBands)
        return;

    auto& band = bands[(size_t) bandIndex];

    switch (field)
    {
        case 0: assignField (band.mode, juce::jlimit (0, pump::heat::kNumTypes - 1, (int) std::lround (value)), dirty); break;
        case 1: assignField (band.drive, juce::jlimit (0.0f, 24.0f, value), dirty); break;
        case 2: assignField (band.mix, juce::jlimit (0.0f, 100.0f, value), dirty); break;
        case 3: assignField (band.level, juce::jlimit (-12.0f, 12.0f, value), dirty); break;
        case 4: assignField (band.solo, value >= 0.5f, dirty); break;
        case 5: assignField (band.mute, value >= 0.5f, dirty); break;
        case 6: assignField (band.bypass, value >= 0.5f, dirty); break;
        default: return;
    }

}

void PumpHeatAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if (dirty)
        applySettings();                // block start, as pump's engine expects

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
