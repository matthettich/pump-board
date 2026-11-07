#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpToneAdapter.h"
#include <cmath>

// Card 5.8's proof, the same one the other adapters made: our adapter and pump's own engine, fed the same signal
// with the same settings, must produce the same samples. Both sides are set up in the same order (prepare, then
// set) because pump's set() steers the quality switch, which fades and re-times the engine.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 96000;        // 2 s: the STFT needs frames, and the FFT is up to 4096 long

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        // A bright tone plus broadband noise: the analyzer has to find something in the highs to tame.
        const float sample = 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 880.0f * (float) t / (float) kRate)
                           + 0.15f * std::sin (2.0f * juce::MathConstants<float>::pi * 5200.0f * (float) t / (float) kRate)
                           + 0.08f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.8f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpToneAdapter&)>& tweak)
{
    pb::PumpToneAdapter adapter;
    tweak (adapter);                    // before prepare: the adapter starts on these settings

    adapter.prepare (kRate, blockSize, 2);

    pump::tone::Engine reference;
    reference.prepare (kRate);
    reference.set (adapter.currentSettings());

    juce::Random adapterRng (0x70E1u), referenceRng (0x70E1u);
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

class ToneNullTests final : public juce::UnitTest
{
public:
    ToneNullTests() : juce::UnitTest ("ToneNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump TONE exposes pump's parameter layout");
        {
            pb::PumpToneAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 11);
            expectEquals (params[0].id, juce::String ("recover"));
            expectEquals (params[0].defaultValue, 30.0f);
            expectEquals (params[1].id, juce::String ("tame"));
            expectEquals (params[2].minValue, -100.0f);         // bias
            expect (params[6].isToggle);                        // auto level
            expectEquals (params[6].defaultValue, 1.0f);
            expectEquals (params[7].maxValue, 12.0f);           // out gain
            expectEquals (params[9].id, juce::String ("quality"));
            expect (params[10].isToggle);                       // delta
            expectEquals (params[10].id, juce::String ("delta"));
        }

        beginTest ("pump TONE reports the STFT size as its latency");
        {
            pb::PumpToneAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);         // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);
            expectEquals (adapter.latencySamples(), 2048);      // quality 0

            adapter.setParam (9, 1.0f);                         // quality 1: asked for, not yet in

            // The switch is the engine's own: a 5 ms fade, the new STFT at a chunk boundary, then a mute for the
            // new size. Until that has run, the pedal really does still work at 2048 (and report it).
            expectEquals (adapter.latencySamples(), 2048);

            pump::tone::Engine reference;
            reference.prepare (kRate);
            reference.set (adapter.currentSettings());

            juce::Random rng (0x91u);
            float worst = 0.0f;

            for (int n = 0; n < 10; ++n)
            {
                juce::AudioBuffer<float> a (2, 512), b (2, 512);
                fillSignal (a, n * 512, rng);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        b.setSample (ch, i, a.getSample (ch, i));

                adapter.process (a);
                reference.process (b.getWritePointer (0), b.getWritePointer (1), 512);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
            }

            expectEquals (adapter.latencySamples(), 4096);      // the switch landed
            expectEquals (adapter.latencySamples(), reference.latency());
            expect (worst <= 3.0e-5f);                           // and it stayed a null test across the switch
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpToneAdapter adapter;
            adapter.prepare (kRate, 64, 1);
            adapter.reset();

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x5u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("tone null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpToneAdapter&) {});
                logMessage ("tone, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("tone null at " + juce::String (blockSize) + " samples, taming the highs");
            {
                const float worst = runNull (blockSize, [] (pb::PumpToneAdapter& adapter)
                {
                    adapter.setParam (0, 15.0f);    // recover
                    adapter.setParam (1, 70.0f);    // tame
                    adapter.setParam (2, 30.0f);    // bias toward taming
                    adapter.setParam (3, -40.0f);   // brighten down
                    adapter.setParam (5, 50.0f);    // speed
                });

                logMessage ("tone, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("tone null at " + juce::String (blockSize) + " samples, 4096 with body");
            {
                const float worst = runNull (blockSize, [] (pb::PumpToneAdapter& adapter)
                {
                    adapter.setParam (0, 80.0f);    // recover
                    adapter.setParam (1, 80.0f);    // tame
                    adapter.setParam (4, 45.0f);    // body up
                    adapter.setParam (5, 40.0f);    // speed
                    adapter.setParam (6, 0.0f);     // auto level off
                    adapter.setParam (8, 70.0f);    // mix
                    adapter.setParam (9, 1.0f);     // quality 1: a structural switch, 85 ms of latency
                });

                logMessage ("tone, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static ToneNullTests toneNullTests;
