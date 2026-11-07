#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpHeatAdapter.h"
#include <cmath>

// Card 5.7's proof, the same one the other adapters made: our adapter and pump's own engine, fed the same signal
// with the same settings, must produce the same samples. HEAT has no settings struct, so both sides are set up
// through the same public setters in the same order — exactly what the adapter does internally.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;        // 1 s: the splitter and oversampler are short, the level match is slow

using pb::PumpHeatAdapter;

void applyLikeAdapter (PumpHeatAdapter& adapter, pump::heat::Engine& engine)
{
    engine.setBands (adapter.bandCount());
    engine.setCrossovers (adapter.crossover (0), adapter.crossover (1), adapter.crossover (2));
    engine.setXoverMode (adapter.crossoverMode());
    engine.setOversampling (adapter.oversamplingIndex());
    engine.setAutoGain (adapter.autoGainOn());
    engine.setMix (adapter.mixPercent());
    engine.setOutput (adapter.outputDb());

    for (int b = 0; b < pump::heat::kNumBands; ++b)
        engine.setBand (b, adapter.bandSettings (b));
}

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;

        const float sample = t < (int) (0.1 * kRate)
                               ? 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) t / (float) kRate)
                               : 0.2512f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.75f * sample);
    }
}

float runNull (int blockSize, const std::function<void (PumpHeatAdapter&)>& tweak)
{
    PumpHeatAdapter adapter;
    tweak (adapter);                    // before prepare: the adapter starts on these settings

    // prepare() prepares pump's engine and then applies the settings, once, in that order.
    // A reset() here would prepare the engine a second time *with* the tweaked settings already in `set[]`, which
    // starts on the new curve — while the reference (prepared with defaults, then told the settings) crossfades
    // into it over 12 ms. That is a different, equally valid state; comparing across it is not a null test.
    adapter.prepare (kRate, blockSize, 2);

    pump::heat::Engine reference;
    reference.prepare (kRate, blockSize);
    applyLikeAdapter (adapter, reference);

    juce::Random adapterRng (0x4EA7u), referenceRng (0x4EA7u);
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

class HeatNullTests final : public juce::UnitTest
{
public:
    HeatNullTests() : juce::UnitTest ("HeatNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump HEAT exposes pump's parameter layout, four bands and all");
        {
            pb::PumpHeatAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 9 + 4 * 7);
            expectEquals (params[0].id, juce::String ("out_gain"));
            expectEquals (params[2].id, juce::String ("oversampling"));
            expectEquals (params[2].defaultValue, 2.0f);        // pump ships at 4x
            expect (params[3].isToggle);                        // auto gain
            expectEquals (params[5].defaultValue, 3.0f);        // three bands
            expectEquals (params[9].id, juce::String ("b1_mode"));
            expectEquals (params[10].id, juce::String ("b1_drive"));
            expectEquals (params[10].defaultValue, 6.0f);
            expectEquals (params[15].id, juce::String ("b1_bypass"));
            expectEquals (params[9 + 7 * 3].id, juce::String ("b4_mode"));
            expectEquals (params.back().id, juce::String ("b4_bypass"));
        }

        beginTest ("pump HEAT reports the splitter and oversampler latency");
        {
            pb::PumpHeatAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);         // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::heat::Engine reference;
            reference.prepare (kRate, 128);
            applyLikeAdapter (adapter, reference);

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0, "4x oversampling is not free");
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpHeatAdapter adapter;
            adapter.prepare (kRate, 64, 1);
            adapter.reset();

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x4u);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("heat null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpHeatAdapter&) {});
                logMessage ("heat, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("heat null at " + juce::String (blockSize) + " samples, tube in the mids");
            {
                const float worst = runNull (blockSize, [] (pb::PumpHeatAdapter& adapter)
                {
                    adapter.setParam (5, 3.0f);         // three bands
                    adapter.setParam (6, 120.0f);       // xover 1
                    adapter.setParam (7, 5000.0f);      // xover 2
                    adapter.setParam (9 + 7 + 0, 2.0f); // band 2 mode: tube
                    adapter.setParam (9 + 7 + 1, 18.0f);// band 2 drive
                    adapter.setParam (9 + 7 + 3, -2.0f);// band 2 level
                    adapter.setParam (1, 60.0f);        // global mix
                });

                logMessage ("heat, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("heat null at " + juce::String (blockSize) + " samples, 8x clip with auto gain off");
            {
                const float worst = runNull (blockSize, [] (pb::PumpHeatAdapter& adapter)
                {
                    adapter.setParam (2, 3.0f);          // 8x oversampling: a structural switch
                    adapter.setParam (3, 0.0f);          // auto gain off
                    adapter.setParam (4, 1.0f);          // linear-phase crossovers
                    adapter.setParam (9 + 0, 5.0f);      // band 1 mode: clip
                    adapter.setParam (9 + 1, 12.0f);     // band 1 drive
                    adapter.setParam (9 + 7 + 5, 1.0f);  // band 2 muted
                });

                logMessage ("heat, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static HeatNullTests heatNullTests;
