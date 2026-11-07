#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpStutterAdapter.h"
#include <cmath>

// Card 5.16's proof: our adapter and pump's own engine, fed the same signal with the same settings and transport,
// must produce the same samples.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s: two beats at 120 BPM, so a repeat can happen

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 261.0f * (float) t / (float) kRate);
        const float sample = tone + 0.06f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.9f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpStutterAdapter&)>& tweak)
{
    pb::PumpStutterAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::stutter::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setTransport (adapter.currentTransport());
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0x57B7u), referenceRng (0x57B7u);
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

class StutterNullTests final : public juce::UnitTest
{
public:
    StutterNullTests() : juce::UnitTest ("StutterNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump STUTTER exposes pump's parameter layout");
        {
            pb::PumpStutterAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 16);
            expectEquals (params[0].id, juce::String ("trigger"));
            expectEquals (params[0].defaultValue, (float) pump::stutter::kAuto);
            expectEquals (params[1].defaultValue, (float) pump::kNote1);
            expectEquals (params[3].defaultValue, (float) pump::kNote4);
            expectEquals (params[11].defaultValue, 100.0f);        // mix
            expect (params[13].isToggle);                          // pattern
            expectEquals (params[14].maxValue, (float) pump::stutter::kUserPattern);
        }

        beginTest ("pump STUTTER adds no latency and takes the adapter's tempo");
        {
            pb::PumpStutterAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);

            adapter.setTempo (140.0);
            expectEquals (adapter.currentTransport().bpm, 140.0);

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);

            pump::stutter::Engine reference;
            reference.prepare (kRate, 128);
            reference.setSettings (adapter.currentSettings());
            expectEquals (adapter.latencySamples(), reference.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpStutterAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x5u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("stutter null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpStutterAdapter&) {});
                logMessage ("stutter, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("stutter null at " + juce::String (blockSize) + " samples, pitched and decaying");
            {
                const float worst = runNull (blockSize, [] (pb::PumpStutterAdapter& adapter)
                {
                    adapter.setParam (1, (float) pump::kNote2);   // interval
                    adapter.setParam (2, 60.0f);                  // chance
                    adapter.setParam (3, (float) pump::kNote8);   // length
                    adapter.setParam (4, (float) pump::kNote32);  // grid
                    adapter.setParam (6, -5.0f);                  // pitch per repeat
                    adapter.setParam (7, 40.0f);                  // decay
                    adapter.setParam (9, 20.0f);                  // variation
                    adapter.setParam (11, 70.0f);                 // mix
                });

                logMessage ("stutter, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("stutter null at " + juce::String (blockSize) + " samples, manual with a pattern");
            {
                const float worst = runNull (blockSize, [] (pb::PumpStutterAdapter& adapter)
                {
                    adapter.setParam (0, (float) pump::stutter::kManual);
                    adapter.setParam (5, 70.0f);                  // gate
                    adapter.setParam (8, -40.0f);                 // filter per repeat
                    adapter.setParam (10, 40.0f);                 // tape stop time
                    adapter.setParam (13, 1.0f);                  // pattern on
                    adapter.setParam (14, 3.0f);                  // pattern 3
                    adapter.setParam (15, (float) pump::kNote8);  // pattern step
                });

                logMessage ("stutter, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static StutterNullTests stutterNullTests;
