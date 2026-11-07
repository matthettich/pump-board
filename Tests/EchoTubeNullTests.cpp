#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/adapters/PumpEchoAdapter.h"
#include "Effects/adapters/PumpTubeAdapter.h"
#include <cmath>
#include <functional>

// Card 1.4's proof, the same one card 1.3 made for the spring: our adapter and pump's own engine, fed the same
// signal with the same settings, must produce the same samples.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 96000;        // 2 s: long enough for echoes to come back

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

template <typename Adapter, typename Engine, typename SettingsType>
float runNull (int blockSize, const std::function<void (Adapter&, SettingsType&)>& tweak)
{
    Adapter adapter;
    adapter.prepare (kRate, blockSize, 2);
    adapter.reset();

    Engine reference;
    reference.prepare (kRate, blockSize);

    SettingsType referenceSettings { adapter.currentSettings() };
    tweak (adapter, referenceSettings);

    adapter.reset();

    // The echo's synced times depend on the tempo; both sides must be told the same one.
    if constexpr (requires (Adapter& a) { a.setTempo (120.0); })
        adapter.setTempo (120.0);

    if constexpr (requires (Engine& e) { e.setTempo (120.0); })
        reference.setTempo (120.0);

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

juce::String describe (float worst)
{
    return juce::String (worst, 10) + " ("
         + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dBFS)";
}
} // namespace

class EchoTubeNullTests final : public juce::UnitTest
{
public:
    EchoTubeNullTests() : juce::UnitTest ("EchoTubeNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump ECHO exposes pump's parameter layout");
        {
            pb::PumpEchoAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 24);
            expectEquals (params[0].id, juce::String ("mode"));
            expectEquals (params[0].maxValue, 4.0f);
            expectEquals (params[1].id, juce::String ("time_l"));
            expectEquals (params[1].maxValue, 4000.0f);
            expect (params[3].isToggle);                        // sync
            expectEquals (params[3].defaultValue, 1.0f);
            expectEquals (params[7].defaultValue, 35.0f);       // feedback
            expectEquals (params[23].id, juce::String ("out_gain"));
            expect (adapter.latencySamples() == 0);
            expectEquals (adapter.tempo(), 120.0);
        }

        beginTest ("pump TUBE exposes pump's parameter layout");
        {
            pb::PumpTubeAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 14);
            expectEquals (params[0].id, juce::String ("mode"));
            expectEquals (params[0].defaultValue, 1.0f);        // Triode
            expectEquals (params[1].maxValue, 36.0f);           // drive
            expect (params[12].isToggle);                       // auto gain
            expectEquals (params[13].id, juce::String ("oversampling"));
            expectEquals (params[13].defaultValue, 2.0f);       // 4x
        }

        beginTest ("pump TUBE reports the oversampler's latency");
        {
            pb::PumpTubeAdapter adapter;
            adapter.prepare (kRate, 128, 2);

            pump::tube::Engine reference;
            reference.prepare (kRate, 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expect (adapter.latencySamples() > 0);
        }

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("echo null at " + juce::String (blockSize) + " samples, default settings");
            {
                const float worst = runNull<pb::PumpEchoAdapter, pump::echo::Engine, pump::echo::Settings> (
                    blockSize, [] (pb::PumpEchoAdapter&, pump::echo::Settings&) {});

                logMessage ("echo, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("echo null at " + juce::String (blockSize) + " samples, tape settings");
            {
                const float worst = runNull<pb::PumpEchoAdapter, pump::echo::Engine, pump::echo::Settings> (
                    blockSize, [] (pb::PumpEchoAdapter& adapter, pump::echo::Settings& reference)
                    {
                        adapter.setParam (0, 1.0f);      // mode: tape
                        adapter.setParam (3, 0.0f);      // sync off
                        adapter.setParam (1, 340.0f);    // time L
                        adapter.setParam (7, 48.0f);     // feedback
                        adapter.setParam (22, 32.0f);    // mix
                        reference = adapter.currentSettings();
                    });

                logMessage ("echo, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("tube null at " + juce::String (blockSize) + " samples, default settings");
            {
                const float worst = runNull<pb::PumpTubeAdapter, pump::tube::Engine, pump::tube::Settings> (
                    blockSize, [] (pb::PumpTubeAdapter&, pump::tube::Settings&) {});

                logMessage ("tube, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }

            beginTest ("tube null at " + juce::String (blockSize) + " samples, crunch settings");
            {
                const float worst = runNull<pb::PumpTubeAdapter, pump::tube::Engine, pump::tube::Settings> (
                    blockSize, [] (pb::PumpTubeAdapter& adapter, pump::tube::Settings& reference)
                    {
                        adapter.setParam (1, 24.0f);     // drive
                        adapter.setParam (5, 2.0f);      // stages
                        adapter.setParam (10, 60.0f);    // mix
                        reference = adapter.currentSettings();
                    });

                logMessage ("tube, block " + juce::String (blockSize) + ": peak difference " + describe (worst));
                expect (worst <= 3.0e-5f);
            }
        }
    }
};

static EchoTubeNullTests echoTubeNullTests;
