#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpChopAdapter.h"
#include <cmath>

// Card 5.17's proof: our adapter and pump's own engine, fed the same signal with the same settings, must produce the
// same samples. CHOP counts bars, so the transport has to match too.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;        // 1 s: enough to cross a slice boundary at 120 BPM

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 147.0f * (float) t / (float) kRate);
        const float sample = tone + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.9f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpChopAdapter&)>& tweak)
{
    pb::PumpChopAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::chop::Engine reference;
    reference.prepare (kRate, blockSize);
    reference.setTransport (adapter.currentTransport());
    reference.setSettings (adapter.currentSettings());

    juce::Random adapterRng (0xC40u), referenceRng (0xC40u);
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

class ChopNullTests final : public juce::UnitTest
{
public:
    ChopNullTests() : juce::UnitTest ("ChopNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump CHOP exposes pump's parameter layout");
        {
            pb::PumpChopAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 7);                  // the board owns the tempo; CHOP has no BPM knob
            expectEquals (params[0].id, juce::String ("slices"));
            expectEquals (params[2].defaultValue, (float) pump::chop::kQ16);
            expect (params[3].isToggle);                        // hold
            expect (params[4].isToggle);                        // two pads loop
            expectEquals (params[5].defaultValue, 100.0f);      // mix is shown 0..100, stored 0..1
            expectEquals (params[6].defaultValue, 0.0f);        // output
        }

        beginTest ("pump CHOP keeps its mix in 0..1 and reports no latency");
        {
            pb::PumpChopAdapter adapter;
            adapter.setParam (5, 50.0f);
            expectWithinAbsoluteError (adapter.currentSettings().mix, 0.5, 1.0e-6);

            adapter.setTempo (90.0);
            expectEquals (adapter.currentTransport().bpm, 90.0);
            expectWithinAbsoluteError (adapter.currentSettings().bpm, 90.0, 1.0e-6);

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpChopAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x1u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("chop null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpChopAdapter&) {});
                logMessage ("chop, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("chop null at " + juce::String (blockSize) + " samples, 16 slices of two bars");
            {
                const float worst = runNull (blockSize, [] (pb::PumpChopAdapter& adapter)
                {
                    adapter.setParam (0, 1.0f);                        // 16 slices
                    adapter.setParam (1, (float) pump::chop::kTwoBars);
                    adapter.setParam (2, (float) pump::chop::kQ4);
                    adapter.setParam (3, 1.0f);                        // hold
                    adapter.setTempo (100.0);                          // the board's tempo, applied by hand here
                });

                logMessage ("chop, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("chop null at " + juce::String (blockSize) + " samples, half bar, no loop");
            {
                const float worst = runNull (blockSize, [] (pb::PumpChopAdapter& adapter)
                {
                    adapter.setParam (1, (float) pump::chop::kHalfBar);
                    adapter.setParam (2, (float) pump::chop::kQ32);
                    adapter.setParam (4, 0.0f);                        // no two-pad loop
                    adapter.setParam (5, 60.0f);                       // mix
                });

                logMessage ("chop, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static ChopNullTests chopNullTests;
