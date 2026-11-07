#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpSwirlAdapter.h"
#include <cmath>

// Card 5.2's proof, the same one cards 1.3, 1.4 and 5.1 made: our adapter and pump's own engine, fed the same
// signal with the same settings, must produce the same samples. Both sides are built in the same order
// (settings, then prepare) so a type change cannot put one of them through a structural fade the other skips.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;        // 1 s: the lines are short, the feedback ring is what needs the time

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        const float sample = t < (int) (0.1 * kRate)
                               ? 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) t / (float) kRate)
                               : 0.2512f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.8f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpSwirlAdapter&)>& tweak)
{
    pb::PumpSwirlAdapter adapter;
    tweak (adapter);                    // before prepare: the adapter starts on these settings

    adapter.prepare (kRate, blockSize, 2);
    adapter.reset();

    pump::swirl::Engine reference;
    pump::Transport transport;
    transport.bpm = 120.0;              // the adapter's own default, so synced types agree
    reference.setSettings (adapter.currentSettings());
    reference.setTransport (transport);
    reference.prepare (kRate, blockSize);

    juce::Random adapterRng (0x5A17u), referenceRng (0x5A17u);
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

class SwirlNullTests final : public juce::UnitTest
{
public:
    SwirlNullTests() : juce::UnitTest ("SwirlNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump SWIRL exposes pump's parameter layout");
        {
            pb::PumpSwirlAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 13);
            expectEquals (params[0].id, juce::String ("mode"));
            expectEquals (params[0].maxValue, 5.0f);            // six types
            expectEquals (params[0].defaultValue, 1.0f);        // Analog
            expectEquals (params[1].id, juce::String ("variant"));
            expectEquals (params[2].maxValue, 12.0f);           // rate
            expect (params[3].isToggle);                        // sync
            expectEquals (params[4].id, juce::String ("division"));
            expectEquals (params[4].defaultValue, 16.0f);       // 1/4, pump's kNote1
            expectEquals (params[4].maxValue, (float) (pump::kNumNotes - 1));
            expectEquals (params[11].defaultValue, 50.0f);      // mix
            expectEquals (params[12].id, juce::String ("out_gain"));
            expectEquals (params[12].minValue, -24.0f);
            expectEquals (adapter.tempo(), 120.0);
        }

        beginTest ("pump SWIRL is zero latency except the through-zero flanger");
        {
            pb::PumpSwirlAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);         // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 0);         // Analog, variant 0

            // THRU-0: flanger (type 3), variant 1. The dry copy is delayed by 3 ms so the swept copy can cross it.
            adapter.setParam (0, 3.0f);
            adapter.setParam (1, 1.0f);
            adapter.reset();

            pump::swirl::Engine reference;
            pump::swirl::Settings s = adapter.currentSettings();
            reference.setSettings (s);
            reference.prepare (kRate, 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expectEquals (adapter.latencySamples(), 144);       // 3 ms at 48 kHz
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpSwirlAdapter adapter;
            adapter.prepare (kRate, 64, 1);
            adapter.reset();

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x2u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("swirl null at " + juce::String (blockSize) + " samples, default analog chorus");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSwirlAdapter&) {});
                logMessage ("swirl, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("swirl null at " + juce::String (blockSize) + " samples, ensemble with drive");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSwirlAdapter& adapter)
                {
                    adapter.setParam (0, 0.0f);     // type: ensemble (bucket brigade, hiss, cross-mix)
                    adapter.setParam (1, 3.0f);     // widest
                    adapter.setParam (2, 0.25f);    // rate
                    adapter.setParam (7, 40.0f);    // feedback
                    adapter.setParam (8, 180.0f);   // spread
                    adapter.setParam (9, 6000.0f);  // tone: high cut on
                    adapter.setParam (10, 70.0f);   // warmth
                    adapter.setParam (11, 60.0f);   // mix
                });

                logMessage ("swirl, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("swirl null at " + juce::String (blockSize) + " samples, through-zero flanger");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSwirlAdapter& adapter)
                {
                    adapter.setParam (0, 3.0f);     // type: flanger
                    adapter.setParam (1, 1.0f);     // through-zero: 3 ms of latency on both sides
                    adapter.setParam (5, 90.0f);    // depth
                    adapter.setParam (7, 30.0f);    // feedback
                    adapter.setParam (11, 50.0f);   // mix
                });

                logMessage ("swirl, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("swirl null at " + juce::String (blockSize) + " samples, tempo-synced phaser");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSwirlAdapter& adapter)
                {
                    adapter.setParam (0, 4.0f);     // type: phaser
                    adapter.setParam (1, 2.0f);     // 8 stages
                    adapter.setParam (3, 1.0f);     // sync on
                    adapter.setParam (4, 8.0f);     // 1/8
                    adapter.setParam (7, 55.0f);    // feedback
                    adapter.setParam (11, 50.0f);   // mix
                });

                logMessage ("swirl, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static SwirlNullTests swirlNullTests;
