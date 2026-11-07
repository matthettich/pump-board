#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpCompAdapter.h"
#include <cmath>

// Card 5.3's proof, the same one the earlier adapters made: our adapter and pump's own engine, fed the same
// signal with the same settings, must produce the same samples. Both sides are built in the same order
// (settings, then prepare), because pump's prepare settles the signal topology from those settings.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 96000;        // 2 s: a slow release needs time to be visible

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        // A loud burst, then a quiet tail: the compressor has to move in both directions.
        const double env = t < (int) (0.4 * kRate) ? 0.6 : 0.05;
        const float sample = (float) (env * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) t / (float) kRate))
                           + 0.2512f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.8f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpCompAdapter&)>& tweak)
{
    pb::PumpCompAdapter adapter;
    tweak (adapter);                    // before prepare: the adapter starts on these settings

    adapter.prepare (kRate, blockSize, 2);
    adapter.reset();

    pump::comp::Engine reference;
    reference.setSettings (adapter.currentSettings());
    reference.prepare (kRate, blockSize);

    juce::Random adapterRng (0xC0ADu), referenceRng (0xC0ADu);
    float worst = 0.0f;

    for (int start = 0; start + blockSize <= kLength; start += blockSize)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize);
        fillSignal (a, start, adapterRng);
        fillSignal (b, start, referenceRng);

        adapter.process (a);
        reference.process (b.getReadPointer (0), b.getReadPointer (1), nullptr, nullptr,
                           b.getWritePointer (0), b.getWritePointer (1), blockSize);

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

class CompNullTests final : public juce::UnitTest
{
public:
    CompNullTests() : juce::UnitTest ("CompNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump COMP exposes pump's parameter layout");
        {
            pb::PumpCompAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 22);
            expectEquals (params[0].id, juce::String ("style"));
            expectEquals (params[0].maxValue, 5.0f);            // six designs
            expectEquals (params[1].id, juce::String ("threshold"));
            expectEquals (params[1].minValue, -60.0f);
            expectEquals (params[1].defaultValue, -18.0f);
            expect (params[7].isToggle);                        // auto release
            expect (params[13].isToggle);                       // auto makeup
            expectEquals (params[9].maxValue, 20.0f);           // lookahead
            expectEquals (params[21].id, juce::String ("oversampling"));
            expectEquals (adapter.latencySamples(), 0);
        }

        beginTest ("pump COMP reports the latency lookahead and oversampling add");
        {
            pb::PumpCompAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);         // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);         // pump's defaults: no lookahead, 1x

            adapter.setParam (9, 2.0f);                         // 2 ms of lookahead
            adapter.reset();

            pump::comp::Engine reference;
            reference.setSettings (adapter.currentSettings());
            reference.prepare (kRate, 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expectEquals (adapter.latencySamples(), 96);        // 2 ms at 48 kHz, no oversampler
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpCompAdapter adapter;
            adapter.prepare (kRate, 64, 1);
            adapter.reset();

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x3u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("comp null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCompAdapter&) {});
                logMessage ("comp, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("comp null at " + juce::String (blockSize) + " samples, master glue");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCompAdapter& adapter)
                {
                    adapter.setParam (0, 1.0f);     // style: glue
                    adapter.setParam (1, -16.0f);   // threshold
                    adapter.setParam (3, 12.0f);    // knee
                    adapter.setParam (5, 30.0f);    // attack
                    adapter.setParam (6, 300.0f);   // release
                    adapter.setParam (7, 1.0f);     // auto release
                    adapter.setParam (4, 6.0f);     // range
                    adapter.setParam (13, 1.0f);    // auto makeup
                });

                logMessage ("comp, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("comp null at " + juce::String (blockSize) + " samples, punch with lookahead and 2x");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCompAdapter& adapter)
                {
                    adapter.setParam (0, 3.0f);     // style: punch (feedback topology)
                    adapter.setParam (1, -22.0f);   // threshold
                    adapter.setParam (2, 4.0f);     // ratio
                    adapter.setParam (3, 6.0f);     // knee
                    adapter.setParam (5, 18.0f);    // attack
                    adapter.setParam (6, 90.0f);    // release
                    adapter.setParam (8, 8.0f);     // hold
                    adapter.setParam (9, 2.0f);     // 2 ms lookahead: real latency
                    adapter.setParam (10, 85.0f);   // parallel mix
                    adapter.setParam (21, 1.0f);    // 2x oversampling: more latency
                    adapter.setParam (13, 1.0f);    // auto makeup
                });

                logMessage ("comp, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("comp null at " + juce::String (blockSize) + " samples, vari-mu in mid/side");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCompAdapter& adapter)
                {
                    adapter.setParam (0, 4.0f);     // style: vari-mu (tube colour)
                    adapter.setParam (1, -24.0f);   // threshold
                    adapter.setParam (2, 2.5f);     // ratio
                    adapter.setParam (3, 18.0f);    // knee
                    adapter.setParam (5, 25.0f);    // attack
                    adapter.setParam (6, 450.0f);   // release
                    adapter.setParam (15, 1.0f);    // channel mode: mid/side
                    adapter.setParam (14, 60.0f);   // stereo link
                    adapter.setParam (13, 1.0f);    // auto makeup
                });

                logMessage ("comp, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static CompNullTests compNullTests;
