#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpTwangAdapter.h"
#include <cmath>

// The owner's report: "changing the spring types in twang breaks the effect". SPRING MODEL is COIL's `model`, which
// goes through `configure(false)` on every setSettings — the spring body re-designs its strings and every voice
// redesigns itself. These tests ask the only question that settles it: does the pedal still ring afterwards?
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 128;

double peakOf (const juce::AudioBuffer<float>& buffer, int channel = 0)
{
    double peak = 0.0;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        peak = std::max (peak, (double) std::abs (buffer.getSample (channel, i)));

    return peak;
}

// A plucked-string-ish burst: an inharmonic 110 Hz burst over the first four blocks of a run, then silence, so the
// springs have something to ring on and the tail can be measured. `sinceRun` counts from the start of the run, so a
// change of spring model mid-test can be followed by a fresh strum the way a player would.
void excite (juce::AudioBuffer<float>& buffer, int sinceRun)
{
    const float amplitude = sinceRun < 4 ? 0.25f : 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float s = amplitude * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f
                                                  * (float) (sinceRun * kBlock + i) / (float) kRate);
        buffer.setSample (0, i, s);
        buffer.setSample (1, i, s);
    }
}
} // namespace

class TwangModelTests final : public juce::UnitTest
{
public:
    TwangModelTests() : juce::UnitTest ("TwangModel", "effects") {}

    void runTest() override
    {
        beginTest ("every spring model rings at a fresh prepare");
        {
            const std::array<const char*, 6> names { "two-spring", "three-spring", "short tank",
                                                     "long tank", "digital", "lo-fi digital" };

            for (int model = 0; model < 6; ++model)
            {
                pb::PumpTwangAdapter adapter;
                adapter.setParam (17, 100.0f);              // MIX fully wet before prepare: no dry path to hide behind
                adapter.setParam (0, (float) model);
                adapter.prepare (kRate, kBlock, 2);

                juce::AudioBuffer<float> buffer (2, kBlock);
                double peak = 0.0;

                for (int b = 0; b < 10; ++b)
                {
                    excite (buffer, b);
                    adapter.process (buffer);
                    if (b >= 4)
                        peak = std::max (peak, peakOf (buffer));
                }

                logMessage ("model " + juce::String (model) + " (" + names[(size_t) model] + "): peak "
                            + juce::String (peak, 6));
                expect (peak > 0.002, "model " + juce::String (model) + " went silent");
                expect (std::isfinite (peak));
            }
        }

        beginTest ("changing the model mid-play does not break a ringing pedal");
        {
            pb::PumpTwangAdapter adapter;
            adapter.setParam (17, 100.0f);
            adapter.prepare (kRate, kBlock, 2);

            juce::AudioBuffer<float> buffer (2, kBlock);

            for (int b = 0; b < 8; ++b)
            {
                excite (buffer, b);
                adapter.process (buffer);
            }

            const double before = peakOf (buffer);

            for (int model = 1; model < 6; ++model)
            {
                adapter.setParam (0, (float) model);

                double peak = 0.0;

                // A player changes the model and then plays: the new springs need the input to ring, exactly as they
                // do at a fresh prepare (in resonator mode the chord is the tuning, not a kick).
                for (int b = 0; b < 12; ++b)
                {
                    excite (buffer, b);
                    adapter.process (buffer);
                    if (b >= 6)
                        peak = std::max (peak, peakOf (buffer));
                }

                logMessage ("after switching to model " + juce::String (model) + ": peak " + juce::String (peak, 6)
                            + " (before: " + juce::String (before, 6) + ")");
                expect (std::isfinite (peak));
                expect (peak > 0.002, "model " + juce::String (model) + " was silent after a live switch");
            }
        }
    }
};

static TwangModelTests twangModelTests;
