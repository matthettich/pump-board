#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpStripAdapter.h"
#include <cmath>

// Card 5.12's proof: our adapter and pump's own engine, fed the same signal with the same settings, must produce the
// same samples. Both sides are set up in the same order (prepare, then settings).
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 330.0f * (float) t / (float) kRate)
                         + 0.2f * std::sin (2.0f * juce::MathConstants<float>::pi * 2650.0f * (float) t / (float) kRate);
        const float sample = tone + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.9f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpStripAdapter&)>& tweak)
{
    pb::PumpStripAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::strip::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0x57u), referenceRng (0x57u);
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

class StripNullTests final : public juce::UnitTest
{
public:
    StripNullTests() : juce::UnitTest ("StripNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump STRIP exposes pump's parameter layout");
        {
            pb::PumpStripAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 37);
            expectEquals (params[0].id, juce::String ("model"));
            expectEquals (params[0].defaultValue, (float) pump::strip::kClassA);
            expectEquals (params[2].defaultValue, 100.0f);         // colour
            expectEquals (params[4].defaultValue, pump::strip::kHpfOff);
            expectEquals (params[5].defaultValue, pump::strip::kLpfOff);
            expect (params[6].isToggle);                           // eq on
            expectEquals (params[11].defaultValue, 400.0f);        // LMF freq
            expectEquals (params[26].defaultValue, 4.0f);          // comp ratio
            expectEquals (params[36].maxValue, 2.0f);              // oversampling
        }

        beginTest ("pump STRIP reports its oversampler as latency");
        {
            pb::PumpStripAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);            // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::strip::Engine reference;
            reference.prepare (kRate, 128);
            reference.setSettings (adapter.currentSettings());

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0);                 // 2x oversampling by default

            // At 1x the oversampler still costs its filter delay (63 samples at 48k), so compare with pump rather
            // than assuming zero.
            adapter.setParam (36, 0.0f);                           // 1x
            adapter.reset();

            pump::strip::Engine unity;
            unity.prepare (kRate, 128);
            unity.setSettings (adapter.currentSettings());

            expectEquals (adapter.latencySamples(), unity.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpStripAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x2u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("strip null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpStripAdapter&) {});
                logMessage ("strip, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("strip null at " + juce::String (blockSize) + " samples, EQ and compressor on");
            {
                const float worst = runNull (blockSize, [] (pb::PumpStripAdapter& adapter)
                {
                    adapter.setParam (0, 1.0f            );   // a coloured model
                    adapter.setParam (1, 6.0f);       // input
                    adapter.setParam (7, 3.5f);       // LF gain
                    adapter.setParam (8, 120.0f);     // LF freq
                    adapter.setParam (10, -4.0f);     // LMF gain
                    adapter.setParam (11, 700.0f);    // LMF freq
                    adapter.setParam (12, 1.8f);      // LMF Q
                    adapter.setParam (13, 2.5f);      // HMF gain
                    adapter.setParam (16, -2.0f);     // HF gain
                    adapter.setParam (24, 1.0f);      // comp on
                    adapter.setParam (25, -24.0f);    // comp threshold
                    adapter.setParam (27, 8.0f);      // comp attack
                    adapter.setParam (30, 4.0f);      // makeup
                    adapter.setParam (35, 0.0f);      // auto gain off
                });

                logMessage ("strip, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("strip null at " + juce::String (blockSize) + " samples, gate, filters and 4x");
            {
                const float worst = runNull (blockSize, [] (pb::PumpStripAdapter& adapter)
                {
                    adapter.setParam (3, 1.0f);       // phase invert
                    adapter.setParam (4, 90.0f);      // HPF
                    adapter.setParam (5, 9000.0f);    // LPF
                    adapter.setParam (19, 1.0f);      // gate on
                    adapter.setParam (20, -40.0f);    // gate threshold
                    adapter.setParam (21, 40.0f);     // gate range
                    adapter.setParam (22, 80.0f);     // gate release
                    adapter.setParam (23, 1.0f);      // gate mode
                    adapter.setParam (32, 150.0f);    // sidechain HPF
                    adapter.setParam (33, 1.0f);      // dynamics first
                    adapter.setParam (36, 2.0f);      // 4x
                });

                logMessage ("strip, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static StripNullTests stripNullTests;
