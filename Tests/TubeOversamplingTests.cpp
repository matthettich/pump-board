#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpTubeAdapter.h"
#include <cmath>

// Card 5.33: the owner's instruction, "stop oversampling from happening mid play". OVERSAMPLING changes how far the
// pedal delays the signal, and nothing compensates latency across slots, so switching it under a held note shifts
// everything after the pedal. It now waits for the input to be silent.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 64;
constexpr int kOversampling = 13;      // PumpTubeAdapter's param order: 1x, 2x, 4x, 8x

void fill (juce::AudioBuffer<float>& buffer, float amplitude)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float s = amplitude * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / (float) kRate);
        buffer.setSample (0, i, s);
        buffer.setSample (1, i, s);
    }
}

void run (pb::PumpTubeAdapter& adapter, int blocks, float amplitude)
{
    juce::AudioBuffer<float> buffer (2, kBlock);

    for (int b = 0; b < blocks; ++b)
    {
        fill (buffer, amplitude);
        adapter.process (buffer);
    }
}
} // namespace

class TubeOversamplingTests final : public juce::UnitTest
{
public:
    TubeOversamplingTests() : juce::UnitTest ("TubeOversampling", "effects") {}

    void runTest() override
    {
        beginTest ("OVERSAMPLING does not change the pedal's latency while audio is running");
        {
            pb::PumpTubeAdapter adapter;
            adapter.prepare (kRate, kBlock, 2);

            run (adapter, 8, 0.2f);

            const int before = adapter.latencySamples();
            expect (before > 0);                       // the default is 4x, so it delays

            adapter.setParam (kOversampling, 3.0f);    // 8x, a different latency
            run (adapter, 16, 0.2f);

            expectEquals (adapter.latencySamples(), before);
            expectEquals (adapter.currentSettings().oversampling, 3);   // the request is remembered
        }

        beginTest ("it applies once the input has been silent, so the switch lands between phrases");
        {
            pb::PumpTubeAdapter adapter;
            adapter.prepare (kRate, kBlock, 2);

            run (adapter, 8, 0.2f);
            const int before = adapter.latencySamples();

            adapter.setParam (kOversampling, 0.0f);    // 1x: no oversampler, no latency at all
            run (adapter, 16, 0.2f);
            expectEquals (adapter.latencySamples(), before);

            run (adapter, 8, 0.0f);                    // silence: now it may switch

            expectEquals (adapter.latencySamples(), 0);
        }

        beginTest ("a change requested before prepare is simply in force");
        {
            pb::PumpTubeAdapter adapter;
            adapter.setParam (kOversampling, 0.0f);
            adapter.prepare (kRate, kBlock, 2);

            expectEquals (adapter.latencySamples(), 0);
            expectEquals (adapter.currentSettings().oversampling, 0);
        }
    }
};

static TubeOversamplingTests tubeOversamplingTests;
