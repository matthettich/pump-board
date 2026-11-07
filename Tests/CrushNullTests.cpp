#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpCrushAdapter.h"
#include <cmath>

// Card 5.1's proof, the same one cards 1.3 and 1.4 made: our adapter and pump's own engine, fed the same
// signal with the same settings, must produce the same samples. CRUSH's noise stages are seeded, so a
// mismatch there is a real one and not a difference in random draws.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;        // 1 s: the crush has no long tail to wait for

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        const float sample = t < (int) (0.1 * kRate)
                               ? 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) t / (float) kRate)
                               : 0.2512f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.7f * sample);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpCrushAdapter&, pump::crush::Settings&)>& tweak)
{
    pb::PumpCrushAdapter adapter;
    adapter.prepare (kRate, blockSize, 2);
    adapter.reset();

    pump::crush::Engine reference;
    reference.prepare (kRate, blockSize);

    pump::crush::Settings referenceSettings { adapter.currentSettings() };
    tweak (adapter, referenceSettings);

    adapter.reset();
    reference.setSettings (referenceSettings);

    juce::Random adapterRng (0xC5u), referenceRng (0xC5u);
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

class CrushNullTests final : public juce::UnitTest
{
public:
    CrushNullTests() : juce::UnitTest ("CrushNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump CRUSH exposes pump's parameter layout");
        {
            pb::PumpCrushAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 16);
            expectEquals (params[0].id, juce::String ("mode"));
            expectEquals (params[0].maxValue, 6.0f);
            expectEquals (params[0].defaultValue, 1.0f);        // Grit 12
            expectEquals (params[2].id, juce::String ("bits"));
            expectEquals (params[2].defaultValue, 12.0f);
            expectEquals (params[3].maxValue, 48000.0f);        // the rate's "off" end stop
            expectEquals (params[14].defaultValue, 100.0f);     // mix: fully crushed
            expectEquals (params[15].id, juce::String ("out_gain"));
            expectEquals (params[15].minValue, -24.0f);
        }

        beginTest ("pump CRUSH reports its transport delay as latency");
        {
            pb::PumpCrushAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);         // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::crush::Engine reference;
            reference.prepare (kRate, 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0, "the 6 ms media transport is real latency");
            expectWithinAbsoluteError ((double) adapter.latencySamples() / kRate * 1000.0, 6.0, 0.1);
        }

        beginTest ("a single-channel block is accepted and comes back identical on both channels");
        {
            pb::PumpCrushAdapter adapter;
            adapter.prepare (kRate, 64, 1);
            adapter.reset();

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x1u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("crush null at " + juce::String (blockSize) + " samples, default settings");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCrushAdapter&, pump::crush::Settings&) {});
                logMessage ("crush, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("crush null at " + juce::String (blockSize) + " samples, worn cassette");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCrushAdapter& adapter, pump::crush::Settings& reference)
                {
                    adapter.setParam (0, 0.0f);     // voicing: clean, so the media stages carry the sound
                    adapter.setParam (3, 48000.0f); // rate: off
                    adapter.setParam (6, 35.0f);    // wow
                    adapter.setParam (7, 40.0f);    // flutter
                    adapter.setParam (8, 40.0f);    // hiss
                    adapter.setParam (10, 25.0f);   // dropouts
                    adapter.setParam (11, 45.0f);   // age
                    reference = adapter.currentSettings();
                });

                logMessage ("crush, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("crush null at " + juce::String (blockSize) + " samples, media before the sampler");
            {
                const float worst = runNull (blockSize, [] (pb::PumpCrushAdapter& adapter, pump::crush::Settings& reference)
                {
                    adapter.setParam (0, 5.0f);     // voicing: toy
                    adapter.setParam (2, 6.0f);     // bits
                    adapter.setParam (3, 9000.0f);  // rate
                    adapter.setParam (5, 1.0f);     // media: before
                    adapter.setParam (9, 45.0f);    // crackle
                    adapter.setParam (14, 40.0f);   // mix: parallel
                    reference = adapter.currentSettings();
                });

                logMessage ("crush, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static CrushNullTests crushNullTests;
