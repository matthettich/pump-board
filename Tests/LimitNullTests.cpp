#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpLimitAdapter.h"
#include <cmath>

// Card 5.10's proof. pump LIMIT has no Settings struct, so the adapter owns one and turns it into engine calls
// through applySettingsTo(); the reference engine gets that same call sequence, which is the only way the two sides
// can be guaranteed to be configured identically.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        // Loud enough that any gain setting actually engages the limiter, with transients for the attack/release.
        const float sine = std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) t / (float) kRate);
        const float burst = (t % 9000) < 400 ? 1.0f : 0.55f;
        const float sample = burst * (0.72f * sine + 0.12f * (rng.nextFloat() * 2.0f - 1.0f));

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.85f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpLimitAdapter&)>& tweak)
{
    pb::PumpLimitAdapter adapter;
    tweak (adapter);                    // before prepare: the adapter starts on these settings

    adapter.prepare (kRate, blockSize, 2);

    pump::limit::Engine reference;
    reference.prepare (kRate, blockSize);
    adapter.applySettingsTo (reference);

    juce::Random adapterRng (0x11A7u), referenceRng (0x11A7u);
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

class LimitNullTests final : public juce::UnitTest
{
public:
    LimitNullTests() : juce::UnitTest ("LimitNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump LIMIT exposes pump's parameter layout");
        {
            pb::PumpLimitAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 14);
            expectEquals (params[0].id, juce::String ("gain"));
            expectEquals (params[1].defaultValue, -1.0f);         // ceiling
            expectEquals (params[2].defaultValue, (float) pump::limit::kAllround);
            expectEquals (params[2].maxValue, (float) (pump::limit::kNumStyles - 1));
            expectEquals (params[3].defaultValue, 5.0f);          // lookahead
            expect (params[8].isToggle);                          // true peak
            expectEquals (params[9].maxValue, 3.0f);              // oversampling
            expect (params[13].isToggle);                         // unity
        }

        beginTest ("pump LIMIT reports its lookahead and oversampling as latency");
        {
            pb::PumpLimitAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);           // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            // The latency is settled by the first configure, which pump runs on the audio thread.
            juce::AudioBuffer<float> block (2, 128);
            block.clear();
            adapter.process (block);

            pump::limit::Engine reference;
            reference.prepare (kRate, 128);
            adapter.applySettingsTo (reference);

            juce::AudioBuffer<float> refBlock (2, 128);
            refBlock.clear();
            reference.process (refBlock.getWritePointer (0), refBlock.getWritePointer (1), 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0);                // 5 ms lookahead through the oversampler
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpLimitAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x9u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.9f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("limit null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpLimitAdapter&) {});
                logMessage ("limit, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("limit null at " + juce::String (blockSize) + " samples, modern and loud");
            {
                const float worst = runNull (blockSize, [] (pb::PumpLimitAdapter& adapter)
                {
                    adapter.setParam (0, 9.0f);                     // gain
                    adapter.setParam (1, -0.5f);                    // ceiling
                    adapter.setParam (2, (float) pump::limit::kModern);
                    adapter.setParam (5, 80.0f);                    // release
                    adapter.setParam (9, 2.0f);                     // 4x oversampling
                });

                logMessage ("limit, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("limit null at " + juce::String (blockSize) + " samples, safe with dither");
            {
                const float worst = runNull (blockSize, [] (pb::PumpLimitAdapter& adapter)
                {
                    adapter.setParam (2, (float) pump::limit::kSafe);
                    adapter.setParam (1, -2.0f);                    // ceiling
                    adapter.setParam (3, 8.0f);                     // lookahead
                    adapter.setParam (4, 100.0f);                   // attack
                    adapter.setParam (5, 400.0f);                   // release
                    adapter.setParam (7, 60.0f);                    // release link
                    adapter.setParam (10, 1.0f);                    // DC filter
                    adapter.setParam (11, 1.0f);                    // 16-bit dither
                    adapter.setParam (12, 1.0f);                    // noise shaping
                });

                logMessage ("limit, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static LimitNullTests limitNullTests;
