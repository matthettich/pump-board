#include "Effects/adapters/PumpStripAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpStripAdapter::params() const
{
    // pump's layout, symbol for symbol (StripParams.hpp), minus bypass. The four EQ bands live in the engine's
    // g[4] / f[4] / q[4] arrays; pump exposes Q only for the two mid bands, and so do we.
    return {
        { "model",       "Model",           0.0f, (float) (pump::strip::kNumModels - 1), (float) pump::strip::kClassA, "", false, 0.0f , pb::choices::names (pump::strip::kModelNames, pump::strip::kNumModels) },
        { "input",       "Input",         -12.0f, 24.0f,      0.0f, "dB", false, 0.0f },
        { "color",       "Colour",          0.0f, 100.0f,   100.0f, "%",  false, 0.0f },
        { "phase",       "Phase Invert",    0.0f, 1.0f,       0.0f, "",   true,  0.0f },
        { "hpf",         "High-Pass",  (float) pump::strip::kHpfOff, 800.0f, (float) pump::strip::kHpfOff, "Hz", false, 0.0f },
        { "lpf",         "Low-Pass",     1000.0f, (float) pump::strip::kLpfOff, (float) pump::strip::kLpfOff, "Hz", false, 0.0f },
        { "eq_on",       "EQ On",           0.0f, 1.0f,       1.0f, "",   true,  0.0f },
        { "lf_gain",     "LF Gain",       -16.0f, 16.0f,      0.0f, "dB", false, 0.0f },
        { "lf_freq",     "LF Freq",        20.0f, 800.0f,   100.0f, "Hz", false, 126.491f },
        { "lf_bell",     "LF Bell",         0.0f, 1.0f,       0.0f, "",   true,  0.0f },
        { "lmf_gain",    "LMF Gain",      -16.0f, 16.0f,      0.0f, "dB", false, 0.0f },
        { "lmf_freq",    "LMF Freq",       60.0f, 3000.0f,  400.0f, "Hz", false, 424.264f },
        { "lmf_q",       "LMF Q",           0.3f, 5.0f,       1.0f, "",   false, 1.224745f },
        { "hmf_gain",    "HMF Gain",      -16.0f, 16.0f,      0.0f, "dB", false, 0.0f },
        { "hmf_freq",    "HMF Freq",      500.0f, 14000.0f, 2500.0f, "Hz", false, 2645.75f },
        { "hmf_q",       "HMF Q",           0.3f, 5.0f,       1.0f, "",   false, 1.224745f },
        { "hf_gain",     "HF Gain",       -16.0f, 16.0f,      0.0f, "dB", false, 0.0f },
        { "hf_freq",     "HF Freq",      1500.0f, 20000.0f, 10000.0f, "Hz", false, 5477.226f },
        { "hf_bell",     "HF Bell",         0.0f, 1.0f,       0.0f, "",   true,  0.0f },
        { "gate_on",     "Gate On",         0.0f, 1.0f,       0.0f, "",   true,  0.0f },
        { "gate_thresh", "Gate Threshold", -80.0f, 0.0f,    -50.0f, "dB", false, 0.0f },
        { "gate_range",  "Gate Range",      0.0f, 80.0f,     20.0f, "dB", false, 0.0f },
        { "gate_release","Gate Release",    5.0f, 2000.0f,  150.0f, "ms", false, 100.0f },
        { "gate_mode",   "Gate Mode",       0.0f, 1.0f, (float) pump::strip::kExpand, "", false, 0.0f , pb::choices::names (pump::strip::kGateModeNames, 2) },
        { "comp_on",     "Compressor On",   0.0f, 1.0f,       0.0f, "",   true,  0.0f },
        { "comp_thresh", "Comp Threshold", -50.0f, 0.0f,    -18.0f, "dB", false, 0.0f },
        { "comp_ratio",  "Comp Ratio",      1.0f, 20.0f,      4.0f, ":1", false, 4.472136f },
        { "comp_attack", "Comp Attack",     0.1f, 100.0f,    10.0f, "ms", false, 3.162278f },
        { "comp_release","Comp Release",   20.0f, 2000.0f,  200.0f, "ms", false, 200.0f },
        { "comp_auto",   "Comp Auto Release", 0.0f, 1.0f,     0.0f, "",   true,  0.0f },
        { "comp_makeup", "Comp Makeup",     0.0f, 24.0f,      0.0f, "dB", false, 0.0f },
        { "comp_mix",    "Comp Mix",        0.0f, 100.0f,   100.0f, "%",  false, 0.0f },
        { "sc_hpf",      "Sidechain High-Pass", (float) pump::strip::kScOff, 500.0f, (float) pump::strip::kScOff, "Hz", false, 0.0f },
        { "order",       "Order",           0.0f, 1.0f, (float) pump::strip::kEqThenDyn, "", false, 0.0f , pb::choices::names (pump::strip::kOrderNames, 2) },
        { "out_gain",    "Output",        -24.0f, 24.0f,      0.0f, "dB", false, 0.0f },
        { "auto_gain",   "Auto Gain",       0.0f, 1.0f,       1.0f, "",   true,  0.0f },
        { "oversampling","Oversampling",    0.0f, 2.0f,       1.0f, "",   false, 0.0f , pb::choices::names (pump::strip::kOsNames, 3) },
    };
}

void PumpStripAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);
    rightScratch.assign ((size_t) preparedBlock, 0.0f);

    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    dirty = true;
    prepared = true;
}

void PumpStripAdapter::reset()
{
    if (! prepared)
        return;

    engine.prepare (preparedRate, preparedBlock);
    engine.setSettings (settings);
    dirty = true;
}

void PumpStripAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kModel:       settings.model      = juce::jlimit (0, pump::strip::kNumModels - 1, (int) std::lround (value)); break;
        case kInput:       settings.input      = juce::jlimit (-12.0f, 24.0f, value); break;
        case kColor:       settings.color      = juce::jlimit (0.0f, 100.0f, value); break;
        case kPhase:       settings.phase      = value >= 0.5f; break;
        case kHpf:         settings.hpf        = juce::jlimit (pump::strip::kHpfOff, 800.0f, value); break;
        case kLpf:         settings.lpf        = juce::jlimit (1000.0f, pump::strip::kLpfOff, value); break;
        case kEqOn:        settings.eqOn       = value >= 0.5f; break;
        case kLfGain:      settings.g[0]       = juce::jlimit (-16.0f, 16.0f, value); break;
        case kLfFreq:      settings.f[0]       = juce::jlimit (20.0f, 800.0f, value); break;
        case kLfBell:      settings.lfBell     = value >= 0.5f; break;
        case kLmfGain:     settings.g[1]       = juce::jlimit (-16.0f, 16.0f, value); break;
        case kLmfFreq:     settings.f[1]       = juce::jlimit (60.0f, 3000.0f, value); break;
        case kLmfQ:        settings.q[1]       = juce::jlimit (0.3f, 5.0f, value); break;
        case kHmfGain:     settings.g[2]       = juce::jlimit (-16.0f, 16.0f, value); break;
        case kHmfFreq:     settings.f[2]       = juce::jlimit (500.0f, 14000.0f, value); break;
        case kHmfQ:        settings.q[2]       = juce::jlimit (0.3f, 5.0f, value); break;
        case kHfGain:      settings.g[3]       = juce::jlimit (-16.0f, 16.0f, value); break;
        case kHfFreq:      settings.f[3]       = juce::jlimit (1500.0f, 20000.0f, value); break;
        case kHfBell:      settings.hfBell     = value >= 0.5f; break;
        case kGateOn:      settings.gateOn     = value >= 0.5f; break;
        case kGateThresh:  settings.gateThresh = juce::jlimit (-80.0f, 0.0f, value); break;
        case kGateRange:   settings.gateRange  = juce::jlimit (0.0f, 80.0f, value); break;
        case kGateRelease: settings.gateRelease = juce::jlimit (5.0f, 2000.0f, value); break;
        case kGateMode:    settings.gateMode   = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kCompOn:      settings.compOn     = value >= 0.5f; break;
        case kCompThresh:  settings.thresh     = juce::jlimit (-50.0f, 0.0f, value); break;
        case kCompRatio:   settings.ratio      = juce::jlimit (1.0f, 20.0f, value); break;
        case kCompAttack:  settings.attack     = juce::jlimit (0.1f, 100.0f, value); break;
        case kCompRelease: settings.release    = juce::jlimit (20.0f, 2000.0f, value); break;
        case kCompAuto:    settings.autoRel    = value >= 0.5f; break;
        case kCompMakeup:  settings.makeup     = juce::jlimit (0.0f, 24.0f, value); break;
        case kCompMix:     settings.mix        = juce::jlimit (0.0f, 100.0f, value); break;
        case kScHpf:       settings.scHpf      = juce::jlimit (pump::strip::kScOff, 500.0f, value); break;
        case kOrder:       settings.order      = juce::jlimit (0, 1, (int) std::lround (value)); break;
        case kOutGain:     settings.outDb      = juce::jlimit (-24.0f, 24.0f, value); break;
        case kAutoGain:    settings.autoGain   = value >= 0.5f; break;
        case kOversampling: settings.oversampling = juce::jlimit (0, 2, (int) std::lround (value)); break;
        default:           return;
    }

    dirty = true;
}

void PumpStripAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
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
