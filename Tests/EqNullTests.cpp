#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpEqAdapter.h"
#include <cmath>

// Card 5.21's proof. pump EQ has no Settings struct, so the adapter owns one and turns it into engine calls through
// applySettingsTo(); the reference engine gets that same call sequence, band for band.
//
// Only pump's zero-latency mode is compared. Linear phase builds its kernels on a worker thread, so two engines can
// legitimately be a frame apart while they settle; docs/NOTES.md records that as untested rather than papered over.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float tone = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f * (float) t / (float) kRate)
                         + 0.2f * std::sin (2.0f * juce::MathConstants<float>::pi * 900.0f * (float) t / (float) kRate)
                         + 0.12f * std::sin (2.0f * juce::MathConstants<float>::pi * 6500.0f * (float) t / (float) kRate);
        const float sample = tone + 0.03f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.85f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpEqAdapter&)>& tweak)
{
    pb::PumpEqAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::eq::Engine reference;
    reference.prepare (kRate, blockSize);
    adapter.applySettingsTo (reference);

    juce::Random adapterRng (0xE9u), referenceRng (0xE9u);
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

class EqNullTests final : public juce::UnitTest
{
public:
    EqNullTests() : juce::UnitTest ("EqNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump EQ exposes pump's globals plus an eight-band block");
        {
            pb::PumpEqAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 7 + pb::PumpEqAdapter::kExposedBands * pb::PumpEqAdapter::kPerBandParams);
            expectEquals (params[0].id, juce::String ("out_gain"));
            expectEquals (params[1].defaultValue, 100.0f);          // gain scale
            expect (params[2].isToggle);                            // auto gain
            expectEquals (params[7].id, juce::String ("b1_used"));
            expectEquals (params[9].defaultValue, 1000.0f);         // band 1 frequency
            expectEquals (params[10].defaultValue, 0.0f);           // band 1 gain
            expectEquals (params[11].defaultValue, 10.0f);          // band 1 Q, pump's own default
            expect (! (params[7].defaultValue > 0.5f));             // bands start unused, as pump ships them
        }

        beginTest ("pump EQ reports no latency in its default zero-latency mode");
        {
            pb::PumpEqAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);             // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::eq::Engine reference;
            reference.prepare (kRate, 128);
            adapter.applySettingsTo (reference);

            expectEquals (adapter.latencySamples(), 0);
            expectEquals (adapter.latencySamples(), reference.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpEqAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0xDu);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("eq null at " + juce::String (blockSize) + " samples, pump's defaults (no bands on)");
            {
                const float worst = runNull (blockSize, [] (pb::PumpEqAdapter&) {});
                logMessage ("eq, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("eq null at " + juce::String (blockSize) + " samples, shelves and bells");
            {
                const float worst = runNull (blockSize, [] (pb::PumpEqAdapter& adapter)
                {
                    adapter.setParam (7, 1.0f);                             // band 1 used
                    adapter.setParam (8, (float) pump::Shape::LowShelf);
                    adapter.setParam (9, 90.0f);
                    adapter.setParam (10, 4.0f);
                    adapter.setParam (13, 1.0f);                            // band 2 used
                    adapter.setParam (15, 700.0f);
                    adapter.setParam (16, -5.0f);
                    adapter.setParam (19, 1.0f);                            // band 3 used
                    adapter.setParam (20, (float) pump::Shape::HighShelf);
                    adapter.setParam (21, 7000.0f);
                    adapter.setParam (22, 3.5f);
                    adapter.setParam (1, 80.0f);                            // gain scale
                });

                logMessage ("eq, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("eq null at " + juce::String (blockSize) + " samples, dynamic cut and a notch");
            {
                const float worst = runNull (blockSize, [] (pb::PumpEqAdapter& adapter)
                {
                    adapter.setParam (3, 0.0f);                             // zero-latency mode (explicit)
                    adapter.setParam (5, 4.0f);                             // dynamic attack
                    adapter.setParam (6, 60.0f);                            // dynamic release
                    adapter.setParam (7, 1.0f);                             // band 1 used
                    adapter.setParam (8, (float) pump::Shape::Bell);
                    adapter.setParam (9, 250.0f);
                    adapter.setParam (10, -6.0f);
                    adapter.setParam (12, 1.0f);                            // band 1 dynamic
                    adapter.setParam (19, 1.0f);                            // band 3 used
                    adapter.setParam (20, (float) pump::Shape::Notch);
                    adapter.setParam (21, 1200.0f);
                    adapter.setParam (23, 18.0f);                           // narrow Q
                });

                logMessage ("eq, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static EqNullTests eqNullTests;
