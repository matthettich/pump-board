#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpVerbAdapter.h"
#include <cmath>

// Card 5.9's proof: our adapter and pump's own engine, fed the same signal with the same settings, must produce the
// same samples. Both sides are set up in the same order (prepare, tempo, settings), which is the lesson HEAT taught.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s: plenty for a 20 ms pre-delay and a reverb build-up

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float sample = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 660.0f * (float) t / (float) kRate)
                           + 0.1f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.7f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpVerbAdapter&)>& tweak)
{
    pb::PumpVerbAdapter adapter;
    tweak (adapter);                    // before prepare: the adapter starts on these settings

    adapter.prepare (kRate, blockSize, 2);

    pump::verb::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setTempo (adapter.tempo());
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0x5E1Bu), referenceRng (0x5E1Bu);
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

class VerbNullTests final : public juce::UnitTest
{
public:
    VerbNullTests() : juce::UnitTest ("VerbNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump VERB exposes pump's parameter layout");
        {
            pb::PumpVerbAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 25);
            expectEquals (params[0].id, juce::String ("mode"));
            expectEquals (params[0].maxValue, (float) (pump::verb::kNumModes - 1));
            expectEquals (params[4].id, juce::String ("size"));
            expectEquals (params[5].defaultValue, 2.2f);          // decay
            expect (params[2].isToggle);                          // pre-delay sync
            expectEquals (params[3].defaultValue, (float) pump::kNote32);
            expectEquals (params[23].defaultValue, 30.0f);        // mix
            expectEquals (params[24].minValue, -24.0f);           // out gain
        }

        beginTest ("pump VERB reports no latency");
        {
            pb::PumpVerbAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);           // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);

            pump::verb::Engine reference;
            reference.prepare (kRate, 128);
            expectEquals (adapter.latencySamples(), reference.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpVerbAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x7u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("verb null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpVerbAdapter&) {});
                logMessage ("verb, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("verb null at " + juce::String (blockSize) + " samples, a long plate");
            {
                const float worst = runNull (blockSize, [] (pb::PumpVerbAdapter& adapter)
                {
                    adapter.setParam (0, (float) pump::verb::kPlate);
                    adapter.setParam (5, 6.0f);         // decay
                    adapter.setParam (6, 90.0f);        // diffusion
                    adapter.setParam (19, 7.0f);        // shimmer pitch
                    adapter.setParam (20, 30.0f);       // shimmer amount
                    adapter.setParam (23, 45.0f);       // mix
                });

                logMessage ("verb, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("verb null at " + juce::String (blockSize) + " samples, gated with ducking");
            {
                const float worst = runNull (blockSize, [] (pb::PumpVerbAdapter& adapter)
                {
                    adapter.setParam (0, (float) pump::verb::kGated);
                    adapter.setParam (18, 200.0f);      // gate time
                    adapter.setParam (21, 60.0f);       // duck
                    adapter.setParam (1, 40.0f);        // pre-delay
                    adapter.setParam (23, 70.0f);       // mix
                });

                logMessage ("verb, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static VerbNullTests verbNullTests;
