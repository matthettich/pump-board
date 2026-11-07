#include "Effects/adapters/PumpTwangAdapter.h"
#include "Effects/adapters/PumpChoices.h"
#include <cmath>
#include <cstring>

namespace pb
{
std::vector<ParamInfo> PumpTwangAdapter::params() const
{
    // pump's TwangParams.hpp, symbol for symbol, minus bypass.
    return {
        { "model",    "Spring Model", 0.0f, 5.0f,   0.0f, "",   false, 0.0f , pb::choices::names (pump::coil::kModelNames, 6) },
        { "tension",  "Tension",    -100.0f, 100.0f, 0.0f, "%",  false, 0.0f },
        { "drip",     "Drip",          0.0f, 100.0f, 50.0f, "%", false, 0.0f },
        { "decay",    "Decay",         0.1f, 15.0f,  2.5f, "s",  false, 1.224745f },
        { "release",  "Release",       0.02f, 8.0f,  0.6f, "s",  false, 0.4f },
        { "harmonic", "Harmonic",      0.0f, 100.0f, 40.0f, "%", false, 0.0f },
        { "spread",   "Spread",        0.0f, 100.0f, 10.0f, "%", false, 0.0f },
        { "wobble",   "Wobble",        0.0f, 100.0f, 15.0f, "%", false, 0.0f },
        { "tone",     "Tone",       -100.0f, 100.0f, 0.0f, "%",  false, 0.0f },
        { "grit",     "Grit",          0.0f, 100.0f, 35.0f, "%", false, 0.0f },
        { "tuning",   "Tuning",        0.0f, (float) (pump::coil::kNumTunings - 1), (float) pump::coil::kTuneChord, "", false, 0.0f , pb::choices::names (pump::coil::kTuningNames, 3) },
        { "root",     "Root",          0.0f, 11.0f,   9.0f, "",  false, 0.0f , pb::choices::names (pump::coil::kRootNames, 12) },
        { "chord",    "Chord",         0.0f, (float) (pump::coil::kNumChords - 1), (float) pump::coil::kChordFifth, "", false, 0.0f , pb::choices::names (pump::coil::kChordNames, 12) },
        { "octave",   "Octave",       -2.0f, 2.0f,    0.0f, "",  false, 0.0f },
        { "drive",    "Drive",         0.0f, 100.0f, 50.0f, "%", false, 0.0f },
        { "tank",     "Tank",          0.0f, 100.0f,  0.0f, "%", false, 0.0f },
        { "width",    "Width",         0.0f, 100.0f, 70.0f, "%", false, 0.0f },
        { "mix",      "Mix",           0.0f, 100.0f, 50.0f, "%", false, 0.0f },
        { "volume",   "Output",      -24.0f, 12.0f,   0.0f, "dB", false, 0.0f },
    };
}

void PumpTwangAdapter::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (numChannels);

    preparedRate = sampleRate;
    preparedBlock = juce::jmax (1, maxBlockSize);

    inLeft.assign ((size_t) preparedBlock, 0.0f);
    inRight.assign ((size_t) preparedBlock, 0.0f);
    outLeft.assign ((size_t) preparedBlock, 0.0f);
    outRight.assign ((size_t) preparedBlock, 0.0f);

    // Settings first, engine second. COIL voices TWANG's chord only when a tuning value *changes* through
    // setSettings, and its own defaults (chord tuning, root A, fifth) are exactly TWANG's, so handing it the same
    // values after prepare() was no change at all — and no chord voices were ever started, which is silence with
    // nothing but the dry path leaking through. prepare() configures fresh and voices the chord itself.
    applySettingsTo (engine);
    engine.prepare (preparedRate, preparedBlock);
    appliedModel = settings.model;
    dirty = false;
    prepared = true;
}

void PumpTwangAdapter::reset()
{
    if (! prepared)
        return;

    applySettingsTo (engine);
    engine.prepare (preparedRate, preparedBlock);   // same order as prepare(): voiced by the fresh configure
    appliedModel = settings.model;
    dirty = false;
}

void PumpTwangAdapter::setParam (int index, float value) noexcept
{
    if (! std::isfinite (value))
        return;

    switch (index)
    {
        case kModel:    assignInt (settings.model, juce::jlimit (0, 5, (int) std::lround (value))); break;
        case kTension:  assignFloat (settings.tension, juce::jlimit (-100.0f, 100.0f, value)); break;
        case kDrip:     assignFloat (settings.drip, juce::jlimit (0.0f, 100.0f, value)); break;
        case kDecay:    assignFloat (settings.decay, juce::jlimit (0.1f, 15.0f, value)); break;
        case kRelease:  assignFloat (settings.release, juce::jlimit (0.02f, 8.0f, value)); break;
        case kHarmonic: assignFloat (settings.harmonic, juce::jlimit (0.0f, 100.0f, value)); break;
        case kSpread:   assignFloat (settings.spread, juce::jlimit (0.0f, 100.0f, value)); break;
        case kWobble:   assignFloat (settings.wobble, juce::jlimit (0.0f, 100.0f, value)); break;
        case kTone:     assignFloat (settings.tone, juce::jlimit (-100.0f, 100.0f, value)); break;
        case kGrit:     assignFloat (settings.grit, juce::jlimit (0.0f, 100.0f, value)); break;
        case kTuning:   assignInt (settings.tuning, juce::jlimit (0, pump::coil::kNumTunings - 1, (int) std::lround (value))); break;
        case kRoot:     assignInt (settings.root, juce::jlimit (0, 11, (int) std::lround (value))); break;
        case kChord:    assignInt (settings.chord, juce::jlimit (0, pump::coil::kNumChords - 1, (int) std::lround (value))); break;
        case kOctave:   assignInt (settings.octave, juce::jlimit (-2, 2, (int) std::lround (value))); break;
        case kDrive:    assignFloat (settings.drive, juce::jlimit (0.0f, 100.0f, value)); break;
        case kTank:     assignFloat (settings.tank, juce::jlimit (0.0f, 100.0f, value)); break;
        case kWidth:    assignFloat (settings.width, juce::jlimit (0.0f, 100.0f, value)); break;
        case kMix:      assignFloat (settings.mix, juce::jlimit (0.0f, 100.0f, value)); break;
        case kVolume:   assignFloat (settings.volumeDb, juce::jlimit (-24.0f, 12.0f, value)); break;
        default:        return;
    }
}

void PumpTwangAdapter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! prepared)
        return;

    if ((size_t) numSamples > inLeft.size())
        return;

    if (dirty)
    {
        const bool modelChanged = settings.model != appliedModel;

        applySettingsTo (engine);
        appliedModel = settings.model;
        dirty = false;

        if (modelChanged && prepared)
            revoice();
    }

    const float* srcL = buffer.getReadPointer (0);
    const float* srcR = numChannels > 1 ? buffer.getReadPointer (1) : srcL;

    std::memcpy (inLeft.data(), srcL, (size_t) numSamples * sizeof (float));
    std::memcpy (inRight.data(), srcR, (size_t) numSamples * sizeof (float));

    // No MIDI events: in resonator mode the input audio is the excitation and the chord is the tuning.
    engine.process (inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), numSamples, nullptr, 0);

    buffer.copyFrom (0, 0, outLeft.data(), numSamples);

    if (numChannels > 1)
        buffer.copyFrom (1, 0, outRight.data(), numSamples);
    // a mono block keeps the left output only; the engine was fed the same signal on both sides
}
} // namespace pb
