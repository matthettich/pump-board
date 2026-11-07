#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpGrainAdapter.h"
#include <cmath>

// Card 5.15's proof: our adapter and pump's own engine, fed the same signal with the same settings and transport,
// must produce the same samples.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s: long enough for several grains at the default density

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) t / (float) kRate)
                         + 0.15f * std::sin (2.0f * juce::MathConstants<float>::pi * 1320.0f * (float) t / (float) kRate);
        const float sample = tone + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.8f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpGrainAdapter&)>& tweak)
{
    pb::PumpGrainAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::grain::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setTransport (adapter.currentTransport());
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0x6EA1u), referenceRng (0x6EA1u);
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

class GrainNullTests final : public juce::UnitTest
{
public:
    GrainNullTests() : juce::UnitTest ("GrainNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump GRAIN exposes pump's parameter layout");
        {
            pb::PumpGrainAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 24);
            expectEquals (params[0].id, juce::String ("position"));
            expectEquals (params[1].defaultValue, 120.0f);         // grain size
            expect (params[4].isToggle);                           // tempo sync
            expectEquals (params[6].defaultValue, (float) pump::grain::kEnvSmooth);
            expectEquals (params[8].defaultValue, 40.0f);          // stereo spread
            expectEquals (params[13].defaultValue, (float) pump::grain::kFilterOff);
            expect (params[21].isToggle);                          // freeze
            expectEquals (params[22].defaultValue, 50.0f);         // mix
        }

        beginTest ("pump GRAIN adds no latency and takes the adapter's tempo");
        {
            pb::PumpGrainAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);

            adapter.setTempo (100.0);
            expectEquals (adapter.currentTransport().bpm, 100.0);

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);

            pump::grain::Engine reference;
            reference.prepare (kRate, 128);
            reference.setSettings (adapter.currentSettings());
            expectEquals (adapter.latencySamples(), reference.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpGrainAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x6u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("grain null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpGrainAdapter&) {});
                logMessage ("grain, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("grain null at " + juce::String (blockSize) + " samples, a pitched minor scale");
            {
                const float worst = runNull (blockSize, [] (pb::PumpGrainAdapter& adapter)
                {
                    adapter.setParam (1, 60.0f);    // size
                    adapter.setParam (2, 25.0f);    // density
                    adapter.setParam (3, 40.0f);    // spray
                    adapter.setParam (9, 7.0f);     // pitch
                    adapter.setParam (10, 5.0f);    // pitch spread
                    adapter.setParam (11, (float) pump::grain::kScaleMinor);
                    adapter.setParam (23, 70.0f);   // mix
                });

                logMessage ("grain, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("grain null at " + juce::String (blockSize) + " samples, synced, filtered, frozen edges");
            {
                const float worst = runNull (blockSize, [] (pb::PumpGrainAdapter& adapter)
                {
                    adapter.setParam (4, 1.0f);     // tempo sync
                    adapter.setParam (5, (float) pump::kNote8);
                    adapter.setParam (6, (float) pump::grain::kEnvPerc);
                    adapter.setParam (13, (float) pump::grain::kFilterLP);
                    adapter.setParam (15, 900.0f);  // cutoff
                    adapter.setParam (16, 45.0f);   // resonance
                    adapter.setParam (17, 50.0f);   // space
                    adapter.setParam (20, 60.0f);   // morph X
                });

                logMessage ("grain, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static GrainNullTests grainNullTests;
