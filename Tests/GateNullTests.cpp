#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpGateAdapter.h"
#include <cmath>

// Card 5.14's proof: our adapter and pump's own engine, fed the same signal with the same settings and the same
// transport, must produce the same samples. The transport is part of the comparison here — a tempo-locked sequencer
// without a matching tempo would not be the same pedal.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) t / (float) kRate)
                         + 0.12f * std::sin (2.0f * juce::MathConstants<float>::pi * 3300.0f * (float) t / (float) kRate);
        const float sample = tone + 0.03f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.85f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpGateAdapter&)>& tweak)
{
    pb::PumpGateAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::gate::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setTransport (adapter.currentTransport());
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0x6A7Eu), referenceRng (0x6A7Eu);
    float worst = 0.0f;

    for (int start = 0; start + blockSize <= kLength; start += blockSize)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize);
        fillSignal (a, start, adapterRng);
        fillSignal (b, start, referenceRng);

        adapter.process (a);
        reference.process (b.getWritePointer (0), b.getWritePointer (1), blockSize);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    }

    return worst;
}

juce::String describe (float worst)
{
    return juce::String (worst, 10) + " ("
         + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dBFS)";
}
} // namespace

class GateNullTests final : public juce::UnitTest
{
public:
    GateNullTests() : juce::UnitTest ("GateNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump GATE exposes pump's parameter layout");
        {
            pb::PumpGateAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 15);
            expectEquals (params[0].id, juce::String ("steps"));
            expectEquals (params[0].maxValue, (float) pump::gate::kMaxSteps);
            expectEquals (params[1].defaultValue, (float) pump::kNote16);
            expectEquals (params[8].defaultValue, (float) pump::gate::kVolume);
            expectEquals (params[9].defaultValue, 250.0f);         // filter low
            expectEquals (params[13].defaultValue, 100.0f);        // mix
        }

        beginTest ("pump GATE runs at the adapter's tempo and adds no latency");
        {
            pb::PumpGateAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);

            adapter.setTempo (90.0);
            expectEquals (adapter.tempo(), 90.0);
            expectEquals (adapter.currentTransport().bpm, 90.0);
            expect (! adapter.currentTransport().playing);         // the board has no host: it free-runs

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpGateAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x8u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("gate null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpGateAdapter&) {});
                logMessage ("gate, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("gate null at " + juce::String (blockSize) + " samples, swung eighths");
            {
                const float worst = runNull (blockSize, [] (pb::PumpGateAdapter& adapter)
                {
                    adapter.setParam (0, 8.0f);                  // steps
                    adapter.setParam (1, (float) pump::kNote8);  // rate
                    adapter.setParam (2, 40.0f);                 // swing
                    adapter.setParam (3, 50.0f);                 // gate length
                    adapter.setParam (7, 80.0f);                 // depth
                });

                logMessage ("gate, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("gate null at " + juce::String (blockSize) + " samples, filter mode with resonance");
            {
                const float worst = runNull (blockSize, [] (pb::PumpGateAdapter& adapter)
                {
                    adapter.setParam (8, (float) pump::gate::kBoth);
                    adapter.setParam (9, 400.0f);                // filter low
                    adapter.setParam (10, 5000.0f);              // filter high
                    adapter.setParam (11, 60.0f);                // resonance
                    adapter.setParam (12, 4.0f);                 // stereo offset
                    adapter.setParam (13, 85.0f);                // mix
                });

                logMessage ("gate, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static GateNullTests gateNullTests;
