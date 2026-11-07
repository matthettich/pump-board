#include "Effects/adapters/PumpCompAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpCompAdapter::params() const
{
    // pump's layout, symbol for symbol (CompParams.hpp). Log-scaled params carry their geometric midpoint as
    // skewCentre, so a knob's centre lands on the same value pump's does.
    return {
        { "style",         "Style",            0.0f,    5.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::comp::kStyleNames, 6) },
        { "threshold",     "Threshold",      -60.0f,    0.0f,  -18.0f, "dB", false, 0.0f },
        { "ratio",         "Ratio",            1.0f,  100.0f,    2.0f, ":1", false, 0.0f },
        { "knee",          "Knee",             0.0f,   36.0f,    6.0f, "dB", false, 0.0f },
        { "range",         "Range",            0.0f,   60.0f,   60.0f, "dB", false, 0.0f },
        { "attack",        "Attack",          0.005f, 250.0f,   10.0f, "ms", false, std::sqrt (0.005f * 250.0f) },
        { "release",       "Release",          5.0f, 2500.0f,  100.0f, "ms", false, std::sqrt (5.0f * 2500.0f) },
        { "auto_release",  "Auto Release",     0.0f,    1.0f,    0.0f, "",   true,  0.0f },
        { "hold",          "Hold",             0.0f,  500.0f,    0.0f, "ms", false, 0.0f },
        { "lookahead",     "Lookahead",        0.0f,   20.0f,    0.0f, "ms", false, 0.0f },
        { "mix",           "Mix",              0.0f,  100.0f,  100.0f, "%",  false, 0.0f },
        { "in_gain",       "Input Gain",     -24.0f,   24.0f,    0.0f, "dB", false, 0.0f },
        { "out_gain",      "Output Gain",    -24.0f,   24.0f,    0.0f, "dB", false, 0.0f },
        { "auto_makeup",   "Auto Makeup",      0.0f,    1.0f,    0.0f, "",   true,  0.0f },
        { "stereo_link",   "Stereo Link",      0.0f,  100.0f,  100.0f, "%",  false, 0.0f },
        { "channel_mode",  "Channel Mode",     0.0f,    3.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::comp::kChannelNames, 4) },
        { "sc_source",     "Sidechain Source", 0.0f,    1.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::comp::kScNames, 2) },
        { "sc_gain",       "Sidechain Gain", -24.0f,   24.0f,    0.0f, "dB", false, 0.0f },
        { "sc_hpf",        "Sidechain High-Pass", 20.0f, 1000.0f, 20.0f, "Hz", false, std::sqrt (20.0f * 1000.0f) },
        { "sc_lpf",        "Sidechain Low-Pass", 1000.0f, 20000.0f, 20000.0f, "Hz", false, std::sqrt (1000.0f * 20000.0f) },
        { "sc_listen",     "Sidechain Listen", 0.0f,    1.0f,    0.0f, "",   true,  0.0f },
        { "oversampling",  "Oversampling",     0.0f,    2.0f,    0.0f, "",   false, 0.0f , pb::choices::names (pump::comp::kOsNames, 3) },
    };
}

void PumpCompAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    // Settings before prepare: pump's prepare reads them to settle the signal topology (lookahead, oversampling,
    // channel mode), so the adapter starts on the settings it was given rather than fading into them.
    engine.setSettings (settings);
    engine.prepare (preparedRate, preparedBlock);

    dirty = true;                       // leave the change pending, as pump's plugin does after prepare
    prepared = true;
}

void PumpCompAdapter::reset()
{
    if (! prepared)
        return;

    // A full re-initialise rather than only clearing state: after reset the adapter must be in exactly the state
    // pump's engine is in after its own prepare (the null test compares those two states).
    engine.setSettings (settings);
    engine.prepare (preparedRate, preparedBlock);
    dirty = true;
}

void PumpCompAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kStyle:        settings.style        = juce::jlimit (0, 5, (int) std::lround (value)); break;
        case kThreshold:    settings.threshold    = (double) value; break;
        case kRatio:        settings.ratio        = (double) value; break;
        case kKnee:         settings.knee         = (double) value; break;
        case kRange:        settings.range        = (double) value; break;
        case kAttack:       settings.attack       = (double) value; break;
        case kRelease:      settings.release      = (double) value; break;
        case kAutoRelease:  settings.autoRelease  = value >= 0.5f; break;
        case kHold:         settings.hold         = (double) value; break;
        case kLookahead:    settings.lookahead    = (double) value; break;
        case kMix:          settings.mix          = (double) value; break;
        case kInGain:       settings.inGain       = (double) value; break;
        case kOutGain:      settings.outGain      = (double) value; break;
        case kAutoMakeup:   settings.autoMakeup   = value >= 0.5f; break;
        case kLink:         settings.link         = (double) value; break;
        case kChannelMode:  settings.channelMode  = juce::jlimit (0, 3, (int) std::lround (value)); break;
        case kScSource:     settings.scExternal   = value >= 0.5f; break;
        case kScGain:       settings.scGain       = (double) value; break;
        case kScHpf:        settings.scHpf        = (double) value; break;
        case kScLpf:        settings.scLpf        = (double) value; break;
        case kScListen:     settings.scListen     = value >= 0.5f; break;
        case kOversampling: settings.oversampling = juce::jlimit (0, 2, (int) std::lround (value)); break;
        default:            return;
    }

    dirty = true;
}

void PumpCompAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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

    // There is no sidechain bus in a slot, so both sidechain inputs are null: pump's engine treats that as
    // silence, which means an External sidechain source simply produces no gain reduction.
    engine.process (left, right, nullptr, nullptr, left, right, numSamples);

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + rightScratch[(size_t) i]);
}
} // namespace pb
