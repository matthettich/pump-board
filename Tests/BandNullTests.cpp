#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpBandAdapter.h"
#include <cmath>

// Card 5.20's proof. pump BAND has no Settings struct, so the adapter owns one and turns it into engine calls through
// applySettingsTo(); the reference engine gets that same call sequence, per-band settings included.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 24000;        // 0.5 s

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const float burst = (t % 10000) < 500 ? 1.0f : 0.5f;
        const float tone = 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f * (float) t / (float) kRate)
                         + 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 1400.0f * (float) t / (float) kRate)
                         + 0.12f * std::sin (2.0f * juce::MathConstants<float>::pi * 9000.0f * (float) t / (float) kRate);
        const float sample = burst * tone + 0.03f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.85f * sample);
    }
}

// Linear-phase crossovers get their kernels from each engine's own worker thread, so two engines told the same
// settings can swap kernels a block or two apart. Compared straight away, the test races those threads (it failed
// in Release on 7 Nov 2026, -25 dBFS, while passing in Debug). Both engines run on silence first, with real time
// for the workers, so they start the comparison on the same kernels with the same (silent) state.
template <class Adapter, class Reference>
void settleOnSilence (Adapter& adapter, Reference& reference, int blockSize)
{
    for (int done = 0, k = 0; done < 24000 || k < 40; done += blockSize, ++k)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize);
        a.clear();
        b.clear();
        adapter.process (a);
        reference.process (b.getWritePointer (0), b.getWritePointer (1), blockSize);
        juce::Thread::sleep (2);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpBandAdapter&)>& tweak)
{
    pb::PumpBandAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::band::Engine reference;
    reference.prepare (kRate, blockSize);
    adapter.applySettingsTo (reference);

    settleOnSilence (adapter, reference, blockSize);

    juce::Random adapterRng (0xBA7Du), referenceRng (0xBA7Du);
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

class BandNullTests final : public juce::UnitTest
{
public:
    BandNullTests() : juce::UnitTest ("BandNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump BAND exposes pump's globals plus a per-band block");
        {
            pb::PumpBandAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 11 + pump::band::kNumBands * pb::PumpBandAdapter::kPerBandParams);
            expectEquals (params[0].id, juce::String ("out_gain"));
            expectEquals (params[3].defaultValue, 3.0f);            // bands
            expectEquals (params[5].defaultValue, 2000.0f);         // crossover 2
            expectEquals (params[11].id, juce::String ("b1_threshold"));
            expectEquals (params[11].defaultValue, 0.0f);
            expectEquals (params[12].defaultValue, 2.0f);           // band 1 ratio, pump's own default
        }

        beginTest ("pump BAND reports the splitter's latency plus its lookahead");
        {
            pb::PumpBandAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);             // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::band::Engine reference;
            reference.prepare (kRate, 128);
            adapter.applySettingsTo (reference);

            expectEquals (adapter.latencySamples(), reference.latency());

            adapter.setParam (7, 4.0f);                             // 4 ms lookahead
            adapter.reset();
            adapter.applySettingsTo (reference);
            expectEquals (adapter.latencySamples(), reference.latency());
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpBandAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0xCu);

            for (int i = 0; i < 64; ++i)
                mono.setSample (0, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("band null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpBandAdapter&) {});
                logMessage ("band, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);      // the card's -90 dBFS
            }

            beginTest ("band null at " + juce::String (blockSize) + " samples, three bands compressed");
            {
                const float worst = runNull (blockSize, [] (pb::PumpBandAdapter& adapter)
                {
                    adapter.setParam (4, 100.0f);   // crossover 1
                    adapter.setParam (5, 1500.0f);  // crossover 2
                    adapter.setParam (11, -18.0f);  // band 1 threshold
                    adapter.setParam (12, 4.0f);    // band 1 ratio
                    adapter.setParam (16, -24.0f);  // band 2 threshold
                    adapter.setParam (17, 6.0f);    // band 2 ratio
                    adapter.setParam (21, -12.0f);  // band 3 threshold
                    adapter.setParam (22, 3.0f);    // band 3 ratio
                    adapter.setParam (9, 70.0f);    // stereo link
                });

                logMessage ("band, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("band null at " + juce::String (blockSize) + " samples, RMS detection with makeup");
            {
                const float worst = runNull (blockSize, [] (pb::PumpBandAdapter& adapter)
                {
                    adapter.setParam (2, 1.0f);     // linear-phase crossovers
                    adapter.setParam (8, 1.0f);     // RMS detection
                    adapter.setParam (10, 1.0f);    // auto makeup
                    adapter.setParam (7, 3.0f);     // lookahead
                    adapter.setParam (15, 3.0f);    // band 1 makeup
                    adapter.setParam (6, 6000.0f);  // crossover 3
                    adapter.setParam (3, 4.0f);     // four bands
                    adapter.setParam (1, 80.0f);    // mix
                });

                logMessage ("band, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static BandNullTests bandNullTests;
