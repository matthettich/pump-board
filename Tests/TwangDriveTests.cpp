#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpTwangAdapter.h"
#include <cmath>

// The user's report: "twang is not making any noise and/or passing audio". The null tests only compare the adapter
// against pump's own engine and assert finiteness — both are silent if the engine is silent. This measures the
// output instead, twice: once the way a Slot drives it (every parameter re-pushed every block) and once without,
// because `Slot::process` pushes all of them every block and anything that reacts to a push reacts every block.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 64;

void sine (juce::AudioBuffer<float>& buffer, int firstSample, float amplitude)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float t = (float) (firstSample + i) / (float) kRate;
        const float s = amplitude * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f * t);

        buffer.setSample (0, i, s);
        buffer.setSample (1, i, s);
    }
}

float runAdapter (bool perBlockPush, int blocks, bool& finite)
{
    pb::PumpTwangAdapter adapter;
    const auto params = adapter.params();

    adapter.setParam (17, 100.0f);          // mix: fully wet, so nothing but the springs can be heard
    adapter.prepare (kRate, kBlock, 2);      // set before prepare: no mix ramp, the dry path is never in the output

    juce::AudioBuffer<float> buffer (2, kBlock);
    float peak = 0.0f;
    finite = true;

    for (int b = 0; b < blocks; ++b)
    {
        sine (buffer, b * kBlock, 0.25f);

        if (perBlockPush)
            for (size_t i = 0; i < params.size(); ++i)
                adapter.setParam ((int) i, i == 17 ? 100.0f : params[i].defaultValue);   // mix stays fully wet

        adapter.process (buffer);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = buffer.getSample (ch, i);
                finite = finite && std::isfinite (s);
                peak = juce::jmax (peak, std::abs (s));
            }
    }

    return peak;
}
} // namespace

class TwangDriveTests final : public juce::UnitTest
{
public:
    TwangDriveTests() : juce::UnitTest ("TwangDrive", "effects") {}

    void runTest() override
    {
        beginTest ("TWANG passes audio when nothing re-pushes its parameters");
        {
            bool finite = false;
            const float peak = runAdapter (false, 16, finite);

            logMessage ("twang wet, no per-block push: peak " + juce::String (peak, 6)
                        + " (" + juce::String (juce::Decibels::gainToDecibels (peak, -200.0f), 1) + " dBFS)");

            expect (finite);
            expect (peak > 0.01f);            // fully wet: the springs alone must ring from a 0.25 sine
        }

        beginTest ("TWANG passes audio when a Slot re-pushes every parameter every block");
        {
            bool finite = false;
            const float peak = runAdapter (true, 16, finite);

            logMessage ("twang wet, per-block push: peak " + juce::String (peak, 6)
                        + " (" + juce::String (juce::Decibels::gainToDecibels (peak, -200.0f), 1) + " dBFS)");

            expect (finite);
            expect (peak > 0.01f);            // the same, with a slot's per-block push on top

            // and the push itself must be inert: unchanged values may not reach COIL, or every block reconfigures
            // the springs under the player (the first version of this test measured 13 dB of difference here).
            bool otherFinite = false;
            const float withoutPush = runAdapter (false, 16, otherFinite);
            expectWithinAbsoluteError (peak, withoutPush, 0.002f);
        }
    }
};

static TwangDriveTests twangDriveTests;
