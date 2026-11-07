#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpSnapAdapter.h"
#include <cmath>

// Card 5.11's proof: our adapter and pump's own engine, fed the same signal with the same settings, must produce the
// same samples. Both sides are set up in the same order (prepare, then settings), the lesson HEAT taught.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        // Picked notes with hard edges: a transient designer needs attacks to find.
        const float burst = (t % 12000) < 600 ? 1.0f : 0.0f;
        const float tone = std::sin (2.0f * juce::MathConstants<float>::pi * 196.0f * (float) t / (float) kRate);
        const float sample = burst * (0.55f * tone + 0.1f * (rng.nextFloat() * 2.0f - 1.0f));

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.9f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpSnapAdapter&)>& tweak)
{
    pb::PumpSnapAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::snap::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0x5A9u), referenceRng (0x5A9u);
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

class SnapNullTests final : public juce::UnitTest
{
public:
    SnapNullTests() : juce::UnitTest ("SnapNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump SNAP exposes pump's parameter layout");
        {
            pb::PumpSnapAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 14);
            expectEquals (params[0].id, juce::String ("attack"));
            expectEquals (params[0].minValue, -100.0f);
            expectEquals (params[2].defaultValue, 10.0f);          // attack time
            expectEquals (params[6].defaultValue, (float) pump::snap::kBandFull);
            expectEquals (params[7].defaultValue, 250.0f);         // crossover
            expect (params[9].isToggle);                           // clip
            expect (params[11].isToggle);                          // delta
        }

        beginTest ("pump SNAP reports its lookahead as latency");
        {
            pb::PumpSnapAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);            // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);            // lookahead off by default

            adapter.setParam (8, 4.0f);                            // 4 ms lookahead
            pump::snap::Engine reference;
            reference.prepare (kRate, 128);
            reference.setSettings (adapter.currentSettings());

            // Looking ahead is a structural change in pump: it fades the output down first and only then moves the
            // delay, so a single block cannot show it. Run both sides until it has settled.
            juce::AudioBuffer<float> block (2, 256), refBlock (2, 256);

            for (int n = 0; n < 16; ++n)
            {
                block.clear();
                refBlock.clear();
                adapter.process (block);
                reference.process (refBlock.getWritePointer (0), refBlock.getWritePointer (1), 256);
            }

            logMessage ("snap lookahead: " + juce::String (adapter.latencySamples()) + " samples, pump "
                        + juce::String (reference.latency()));

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0);
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpSnapAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x3u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("snap null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSnapAdapter&) {});
                logMessage ("snap, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("snap null at " + juce::String (blockSize) + " samples, more attack and less sustain");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSnapAdapter& adapter)
                {
                    adapter.setParam (0, 65.0f);    // attack
                    adapter.setParam (1, -45.0f);   // sustain
                    adapter.setParam (2, 4.0f);     // attack time
                    adapter.setParam (3, 300.0f);   // release
                    adapter.setParam (8, 3.0f);     // lookahead
                });

                logMessage ("snap, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("snap null at " + juce::String (blockSize) + " samples, high band with clipping");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSnapAdapter& adapter)
                {
                    adapter.setParam (5, (float) pump::snap::kFocusHigh);
                    adapter.setParam (6, (float) pump::snap::kBandHigh);
                    adapter.setParam (7, 1200.0f);  // crossover
                    adapter.setParam (9, 1.0f);     // clip
                    adapter.setParam (10, -2.0f);   // ceiling
                    adapter.setParam (12, 80.0f);   // mix
                });

                logMessage ("snap, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static SnapNullTests snapNullTests;
