#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpTwangAdapter.h"
#include <cmath>

// Card 5.21's proof. TWANG is pump's resonator on COIL's engine, and it takes no MIDI here: the input audio is what
// excites the springs, exactly as pump's plugin behaves with an empty keyboard. The reference is COIL's engine
// prepared the same way and given the same audio, plus the same settings through the shared applySettingsTo().
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 12000;        // 0.25 s — COIL is the heaviest engine here; chrome is not the point

void fillSignal (juce::AudioBuffer<float>& buffer, juce::Random& rng)
{
    static constexpr float freqs[] = { 196.0f, 330.0f, 659.0f, 1400.0f, 3100.0f };

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float left = 0.0f, right = 0.0f;

        for (const float f : freqs)
        {
            left  += 0.09f * std::sin (2.0f * juce::MathConstants<float>::pi * f * (float) i / (float) kRate);
            right += 0.09f * std::sin (2.0f * juce::MathConstants<float>::pi * (f * 1.003f) * (float) i / (float) kRate);
        }

        left  += 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
        right += 0.02f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, left);

        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, i, right);
    }
}

float runNull (int blockSize, const std::function<void (pb::PumpTwangAdapter&)>& tweak)
{
    pb::PumpTwangAdapter adapter;
    tweak (adapter);

    adapter.prepare (kRate, blockSize, 2);

    pump::coil::Engine reference;
    adapter.applySettingsTo (reference);      // as the adapter does now: settings first, so prepare() voices the chord
    reference.prepare (kRate, blockSize);

    juce::Random adapterRng (0x7A21u), referenceRng (0x7A21u);
    float worst = 0.0f;

    std::vector<float> refInL ((size_t) blockSize), refInR ((size_t) blockSize);
    std::vector<float> refOutL ((size_t) blockSize), refOutR ((size_t) blockSize);

    for (int start = 0; start + blockSize <= kLength; start += blockSize)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize);
        fillSignal (a, adapterRng);
        fillSignal (b, referenceRng);

        adapter.process (a);

        std::memcpy (refInL.data(), b.getReadPointer (0), (size_t) blockSize * sizeof (float));
        std::memcpy (refInR.data(), b.getReadPointer (1), (size_t) blockSize * sizeof (float));
        reference.process (refInL.data(), refInR.data(), refOutL.data(), refOutR.data(), blockSize, nullptr, 0);

        for (int i = 0; i < blockSize; ++i)
        {
            worst = juce::jmax (worst, std::abs (a.getSample (0, i) - refOutL[(size_t) i]));
            worst = juce::jmax (worst, std::abs (a.getSample (1, i) - refOutR[(size_t) i]));
        }
    }

    return worst;
}

juce::String describe (float worst)
{
    return juce::String (worst, 10) + " ("
         + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dBFS)";
}
} // namespace

class TwangNullTests final : public juce::UnitTest
{
public:
    TwangNullTests() : juce::UnitTest ("TwangNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump TWANG exposes pump's parameter layout");
        {
            pb::PumpTwangAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 19);
            expectEquals (params[0].id, juce::String ("model"));
            expectEquals (params[3].defaultValue, 2.5f);                       // decay
            expectEquals (params[11].defaultValue, 9.0f);                      // root A
            expectEquals (params[12].defaultValue, (float) pump::coil::kChordFifth);
            expectEquals (params[17].defaultValue, 50.0f);                     // mix
        }

        beginTest ("pump TWANG reports no latency and a decay-long tail");
        {
            pb::PumpTwangAdapter adapter;
            adapter.prepare (kRate, 128, 2);

            expectEquals (adapter.latencySamples(), 0);
            expect (adapter.tailSeconds() > 0.0);
        }

        beginTest ("the resonator rings from audio input, with no MIDI at all");
        {
            pb::PumpTwangAdapter adapter;
            adapter.prepare (kRate, 128, 2);

            juce::AudioBuffer<float> block (2, 128);
            juce::Random rng (0x31u);
            fillSignal (block, rng);
            block.applyGain (4.0f);                                            // a hard strum

            adapter.process (block);

            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    finite = finite && std::isfinite (block.getSample (ch, i));

            expect (finite);
        }

        beginTest ("a single-channel block is accepted");
        {
            pb::PumpTwangAdapter adapter;
            adapter.prepare (kRate, 64, 1);

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x32u);
            fillSignal (mono, rng);

            adapter.process (mono);
            expect (std::isfinite (mono.getSample (0, 0)));
        }

        beginTest ("a block longer than prepare() was told about is dropped, not overrun");
        {
            pb::PumpTwangAdapter adapter;
            adapter.prepare (kRate, 64, 2);

            juce::AudioBuffer<float> big (2, 128);
            big.clear();
            big.setSample (0, 0, 0.5f);

            adapter.process (big);
            expectEquals (big.getSample (0, 0), 0.5f);                        // untouched
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("twang null at " + juce::String (blockSize) + " samples, pump's defaults");
            {
                const float worst = runNull (blockSize, [] (pb::PumpTwangAdapter&) {});
                logMessage ("twang, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("twang null at " + juce::String (blockSize) + " samples, a long bright chord");
            {
                const float worst = runNull (blockSize, [] (pb::PumpTwangAdapter& adapter)
                {
                    adapter.setParam (3, 7.0f);      // decay
                    adapter.setParam (5, 80.0f);     // harmonic
                    adapter.setParam (6, 45.0f);     // spread
                    adapter.setParam (12, (float) pump::coil::kChordMaj7);
                    adapter.setParam (15, 40.0f);    // tank
                    adapter.setParam (16, 100.0f);   // width
                    adapter.setParam (17, 75.0f);    // mix
                });

                logMessage ("twang, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("twang null at " + juce::String (blockSize) + " samples, single string, gritty");
            {
                const float worst = runNull (blockSize, [] (pb::PumpTwangAdapter& adapter)
                {
                    adapter.setParam (0, 2.0f);      // another spring model
                    adapter.setParam (1, 45.0f);     // tension
                    adapter.setParam (9, 85.0f);     // grit
                    adapter.setParam (10, (float) pump::coil::kTuneMidi);
                    adapter.setParam (12, (float) pump::coil::kChordSingle);
                    adapter.setParam (14, 20.0f);    // drive (COIL's dwell)
                    adapter.setParam (18, -6.0f);    // output
                });

                logMessage ("twang, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static TwangNullTests twangNullTests;
