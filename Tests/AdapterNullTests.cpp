#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpSpringAdapter.h"
#include <cmath>

// Card 1.3's proof: our adapter and pump's own engine, fed the same signal with the same settings, must
// produce the same samples. A difference would mean the adapter mangles audio or trips over block sizes.
class AdapterNullTests final : public juce::UnitTest
{
public:
    AdapterNullTests() : juce::UnitTest ("AdapterNull", "effects") {}

    static constexpr double kRate = 48000.0;
    static constexpr int kLength = 96000;        // 2 s

    // A 100 ms 440 Hz burst, then seeded noise at -12 dBFS: tests the tank's tail and the grit path.
    static void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
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

    static float runNull (int blockSize, const std::function<void (pb::PumpSpringAdapter&, pump::spring::Settings&)>& tweak)
    {
        pb::PumpSpringAdapter adapter;
        adapter.prepare (kRate, blockSize, 2);
        adapter.reset();

        pump::spring::Engine reference;
        reference.prepare (kRate, blockSize);

        pump::spring::Settings referenceSettings { adapter.currentSettings() };
        tweak (adapter, referenceSettings);

        adapter.reset();
        reference.setSettings (referenceSettings);

        juce::Random adapterRng (0x51A7u), referenceRng (0x51A7u);
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

    void runTest() override
    {
        beginTest ("the adapter exposes pump's parameter layout");
        {
            pb::PumpSpringAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 13);
            expectEquals (params[0].id, juce::String ("model"));
            expectEquals (params[0].maxValue, 5.0f);
            expectEquals (params[12].id, juce::String ("out_gain"));
            expectEquals (params[12].minValue, -24.0f);
            expectEquals (params[11].defaultValue, 30.0f);      // mix
            expect (adapter.latencySamples() == 0);
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("null at " + juce::String (blockSize) + " samples, default settings");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSpringAdapter&, pump::spring::Settings&) {});
                logMessage ("block " + juce::String (blockSize) + ": peak difference "
                            + juce::String (worst, 10) + " (" + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dBFS)");
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("null at " + juce::String (blockSize) + " samples, edited through setParam");
            {
                const float worst = runNull (blockSize, [] (pb::PumpSpringAdapter& adapter, pump::spring::Settings& reference)
                {
                    adapter.setParam (0, 2.0f);    // model: short tank
                    adapter.setParam (2, 4.0f);    // decay: 4 s
                    adapter.setParam (11, 60.0f);  // mix: 60%
                    reference = adapter.currentSettings();
                });

                logMessage ("block " + juce::String (blockSize) + ": peak difference "
                            + juce::String (worst, 10) + " (" + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dBFS)");
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static AdapterNullTests adapterNullTests;
