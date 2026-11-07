#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpCalmAdapter.h"
#include <cmath>

// Card 5.13's proof: our adapter and pump's own engine, fed the same signal with the same settings, must produce the
// same samples. Both sides are set up in the same order (prepare, then set) — CALM, like TONE, steers its FFT size
// from the settings, so the order is part of the contract.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 330.0f * (float) t / (float) kRate)
                         + 0.15f * std::sin (2.0f * juce::MathConstants<float>::pi * 3100.0f * (float) t / (float) kRate)
                         + 0.1f * std::sin (2.0f * juce::MathConstants<float>::pi * 4700.0f * (float) t / (float) kRate);
        const float sample = tone + 0.04f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.85f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpCalmAdapter&)>& tweak)
{
    pb::PumpCalmAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::calm::Engine reference;
    reference.prepare (kRate);
    reference.set (adapter.currentSettings());

    juce::Random adapterRng (0xCA1u), referenceRng (0xCA1u);
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

class CalmNullTests final : public juce::UnitTest
{
public:
    CalmNullTests() : juce::UnitTest ("CalmNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump CALM exposes pump's parameter layout");
        {
            pb::PumpCalmAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 14);
            expectEquals (params[0].id, juce::String ("depth"));
            expectEquals (params[0].defaultValue, 50.0f);
            expectEquals (params[5].defaultValue, (float) pump::calm::kSoft);
            expectEquals (params[7].defaultValue, 20000.0f);       // focus high
            expectEquals (params[8].defaultValue, (float) pump::calm::kLinked);
            expectEquals (params[10].maxValue, 3.0f);              // quality
            expect (params[11].isToggle);                          // delta
            expectEquals (params[13].minValue, -12.0f);            // trim
        }

        beginTest ("pump CALM reports its FFT size as latency");
        {
            pb::PumpCalmAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);            // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::calm::Engine reference;
            reference.prepare (kRate);
            reference.set (adapter.currentSettings());

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0);

            adapter.setParam (10, 0.0f);                           // the smallest FFT
            adapter.reset();

            pump::calm::Engine small;
            small.prepare (kRate);
            small.set (adapter.currentSettings());

            expectEquals (adapter.latencySamples(), small.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpCalmAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x4u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("calm null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCalmAdapter&) {});
                logMessage ("calm, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("calm null at " + juce::String (blockSize) + " samples, hunting high mids");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCalmAdapter& adapter)
                {
                    adapter.setParam (0, 75.0f);    // depth
                    adapter.setParam (2, 70.0f);    // selectivity
                    adapter.setParam (3, 4.0f);     // attack
                    adapter.setParam (4, 60.0f);    // release
                    adapter.setParam (6, 500.0f);   // focus low
                    adapter.setParam (7, 6000.0f);  // focus high
                });

                logMessage ("calm, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("calm null at " + juce::String (blockSize) + " samples, side only with delta");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCalmAdapter& adapter)
                {
                    adapter.setParam (5, 1.0f);     // the other mode
                    adapter.setParam (8, 2.0f);     // side only
                    adapter.setParam (9, 40.0f);    // balance
                    adapter.setParam (10, 3.0f);    // the largest FFT
                    adapter.setParam (11, 1.0f);    // delta
                    adapter.setParam (12, 70.0f);   // mix
                });

                logMessage ("calm, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static CalmNullTests calmNullTests;
