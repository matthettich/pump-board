#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpWideAdapter.h"
#include <cmath>

// Card 5.19's proof. pump WIDE has no Settings struct, so the adapter owns one and turns it into engine calls through
// applySettingsTo(); the reference engine gets that same call sequence.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float left = 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) t / (float) kRate)
                         + 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
        const float right = 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 330.0f * (float) t / (float) kRate)
                          + 0.1f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, left);
        buffer.setSample (1, i, right);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpWideAdapter&)>& tweak)
{
    pb::PumpWideAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::wide::Engine reference;
    reference.prepare (kRate, blockSize);
    adapter.applySettingsTo (reference);

    juce::Random adapterRng (0x41DEu), referenceRng (0x41DEu);
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

class WideNullTests final : public juce::UnitTest
{
public:
    WideNullTests() : juce::UnitTest ("WideNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump WIDE exposes pump's parameter layout");
        {
            pb::PumpWideAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 10 + pump::wide::kNumBands * 2);
            expectEquals (params[0].id, juce::String ("out_gain"));
            expectEquals (params[6].defaultValue, (float) pump::wide::kNumBands);
            expectEquals (params[7].defaultValue, 120.0f);         // crossover 1
            expectEquals (params[10].id, juce::String ("b1_width"));
            expectEquals (params[10].defaultValue, 100.0f);
            expectEquals (params[11].id, juce::String ("b1_stereoize"));
        }

        beginTest ("pump WIDE reports its splitter's latency");
        {
            pb::PumpWideAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);            // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::wide::Engine reference;
            reference.prepare (kRate, 128);
            adapter.applySettingsTo (reference);

            expectEquals (adapter.latencySamples(), reference.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpWideAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0xBu);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("wide null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpWideAdapter&) {});
                logMessage ("wide, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("wide null at " + juce::String (blockSize) + " samples, three bands with mono bass");
            {
                const float worst = runNull (blockSize, [] (pb::PumpWideAdapter& adapter)
                {
                    adapter.setParam (2, 1.0f);      // mono bass on
                    adapter.setParam (3, 140.0f);    // mono frequency
                    adapter.setParam (4, 1.0f);      // steeper slope
                    adapter.setParam (6, 3.0f);      // three bands
                    adapter.setParam (7, 250.0f);    // crossover 1
                    adapter.setParam (8, 3000.0f);   // crossover 2
                    adapter.setParam (10, 120.0f);   // band 1 width
                    adapter.setParam (13, 140.0f);   // band 2 width
                    adapter.setParam (15, 35.0f);    // band 3 stereoize
                });

                logMessage ("wide, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("wide null at " + juce::String (blockSize) + " samples, two bands, balanced");
            {
                const float worst = runNull (blockSize, [] (pb::PumpWideAdapter& adapter)
                {
                    adapter.setParam (1, -30.0f);    // balance
                    adapter.setParam (5, 1.0f);      // the other crossover mode
                    adapter.setParam (6, 2.0f);      // two bands
                    adapter.setParam (10, 160.0f);   // band 1 width
                    adapter.setParam (12, 150.0f);   // band 2 width
                });

                logMessage ("wide, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static WideNullTests wideNullTests;
