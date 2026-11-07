#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/AllocationGuard.h"
#include "Effects/adapters/PumpTuneAdapter.h"
#include <cmath>
#include <functional>
#include <limits>

// pump TUNE behind IEffect: the adapter and pump's own engine, fed the same signal with the same settings, must produce
// the same samples. TUNE's humanize drift is seeded in prepare(), so a mismatch is a real one.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;          // 1 s: enough for the tracker to lock and the correction to move

// A sung vowel a little flat of F#3 (about 183 Hz against 185 Hz), with vibrato and six harmonics, then a noise burst.
// Built from the sample index alone, so the adapter and the reference see identical input.
void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const double phase = twoPi * 183.0 * (double) t / kRate + 1.5 * std::sin (twoPi * 5.0 * (double) t / kRate);
        double v = 0.0;

        for (int k = 1; k <= 6; ++k)
            v += std::sin ((double) k * phase) / (double) k;

        float sample = 0.25f * (float) v;

        if (t > (int) (0.8 * kRate))
            sample = 0.1f * (rng.nextFloat() * 2.0f - 1.0f);

        buffer.setSample (0, i, sample);
        buffer.setSample (1, i, 0.7f * sample);
    }
}

int indexOf (const pb::IEffect& effect, const juce::String& id)
{
    const auto params = effect.params();

    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == id)
            return (int) i;

    return -1;
}

struct NullResult
{
    float worst = 0.0f;
    float changedFromInput = 0.0f;       // the biggest |output - input|: the pedal really did something
};

NullResult runNull (int blockSize, const std::function<void (pb::PumpTuneAdapter&)>& tweak)
{
    pb::PumpTuneAdapter adapter;
    adapter.prepare (kRate, blockSize, 2);
    tweak (adapter);
    adapter.reset();                     // takes the edited settings, exactly like pump's own prepare

    pump::tune::Engine reference;
    reference.setSettings (adapter.currentSettings());
    reference.prepare (kRate, blockSize);

    juce::Random adapterRng (0x7Eu), referenceRng (0x7Eu);
    NullResult result;

    for (int start = 0; start + blockSize <= kLength; start += blockSize)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize), input (2, blockSize);
        fillSignal (a, start, adapterRng);
        fillSignal (b, start, referenceRng);
        input.makeCopyOf (a);

        adapter.process (a);
        reference.process (b.getReadPointer (0), b.getReadPointer (1), b.getWritePointer (0), b.getWritePointer (1), blockSize);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                result.worst = juce::jmax (result.worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
                result.changedFromInput = juce::jmax (result.changedFromInput, std::abs (a.getSample (ch, i) - input.getSample (ch, i)));
            }
    }

    return result;
}

juce::String describe (float worst)
{
    return juce::String (worst, 10) + " (" + juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1) + " dBFS)";
}
} // namespace

class TuneNullTests final : public juce::UnitTest
{
public:
    TuneNullTests() : juce::UnitTest ("TuneNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump TUNE exposes pump's parameter layout, minus what needs MIDI or a window");
        {
            pb::PumpTuneAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 29);
            expectEquals (adapter.typeId(), juce::String ("pump.tune"));
            expectEquals (adapter.displayName(), juce::String ("pump TUNE"));

            expectEquals (indexOf (adapter, "mode"), -1);        // Graph needs the note editor
            expectEquals (indexOf (adapter, "midi"), -1);        // no MIDI into the audio path
            expectEquals (indexOf (adapter, "range"), -1);       // would rebuild buffers in setSettings
            expectEquals (indexOf (adapter, "bypass"), -1);

            const auto& scale = params[(size_t) indexOf (adapter, "scale")];
            expectEquals (scale.choiceCount(), (int) pump::pitch::kNumScales);
            expectEquals (scale.choiceLabel (1.0f), juce::String ("Major"));
            expectEquals (params[(size_t) indexOf (adapter, "key")].choiceLabel (9.0f), juce::String ("A"));
            expectEquals (params[(size_t) indexOf (adapter, "harm_mode")].choiceLabel (1.0f), juce::String ("Scale"));
            expectEquals (params[(size_t) indexOf (adapter, "retune")].defaultValue, 20.0f);
            expectEquals (params[(size_t) indexOf (adapter, "harm_level")].minValue, -24.0f);

            for (const auto& p : params)
                expect (p.defaultValue >= p.minValue && p.defaultValue <= p.maxValue, p.id + " default is in range");
        }

        beginTest ("setParam fills pump's Settings the way PluginTune::settings() does");
        {
            pb::PumpTuneAdapter adapter;
            adapter.prepare (kRate, 128, 2);

            adapter.setParam (indexOf (adapter, "key"), 9.0f);
            adapter.setParam (indexOf (adapter, "scale"), 2.0f);
            adapter.setParam (indexOf (adapter, "retune"), 0.0f);
            adapter.setParam (indexOf (adapter, "amount"), 80.0f);
            adapter.setParam (indexOf (adapter, "transpose"), -5.4f);          // an integer param: rounds
            adapter.setParam (indexOf (adapter, "formant_follow"), 1.0f);
            adapter.setParam (indexOf (adapter, "harm_mode"), 2.0f);
            adapter.setParam (indexOf (adapter, "h2"), 7.0f);
            adapter.setParam (indexOf (adapter, "h3_level"), 55.0f);
            adapter.setParam (indexOf (adapter, "h4_pan"), -20.0f);
            adapter.setParam (indexOf (adapter, "harm_level"), -6.0f);
            adapter.setParam (indexOf (adapter, "out_gain"), 3.0f);

            const auto& s = adapter.currentSettings();
            expectEquals (s.key, 9);
            expectEquals (s.scale, 2);
            expectEquals (s.retuneMs, 0.0);
            expectEquals (s.amount, 80.0);
            expectEquals (s.transpose, -5);
            expect (s.formantFollow);
            expectEquals (s.harmMode, (int) pump::tune::kHarmFixed);
            expectEquals (s.interval[1], 7);
            expectEquals (s.level[2], 55.0);
            expectEquals (s.pan[3], -20.0);
            expectEquals (s.harmLevelDb, -6.0);
            expectEquals (s.outDb, 3.0);

            // what the pedal cannot offer stays where pump's defaults put it
            expectEquals (s.mode, (int) pump::tune::kLive);
            expectEquals (s.midi, (int) pump::tune::kMidiOff);
            expectEquals (s.range, (int) pump::tune::kRangeMid);

            adapter.setParam (indexOf (adapter, "scale"), 99.0f);              // clamped, never out of range
            expectEquals (adapter.currentSettings().scale, (int) pump::pitch::kNumScales - 1);
            adapter.setParam (indexOf (adapter, "amount"), std::numeric_limits<float>::quiet_NaN());   // ignored
            expectEquals (adapter.currentSettings().amount, 80.0);
        }

        beginTest ("pump TUNE reports the PSOLA delay as latency");
        {
            pb::PumpTuneAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);          // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::tune::Engine reference;
            reference.prepare (kRate, 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expectEquals (adapter.latencySamples(), pump::tune::Engine::latencyFor (kRate, pump::tune::kRangeMid));
            expectWithinAbsoluteError ((double) adapter.latencySamples() / kRate * 1000.0, 19.2, 0.5);
        }

        beginTest ("a single-channel block is accepted and comes back finite");
        {
            pb::PumpTuneAdapter adapter;
            adapter.prepare (kRate, 64, 1);
            adapter.reset();

            juce::AudioBuffer<float> mono (1, 64);
            juce::Random rng (0x1u);

            for (int block = 0; block < 20; ++block)
            {
                for (int i = 0; i < 64; ++i)
                    mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

                adapter.process (mono);
            }

            expect (std::isfinite (mono.getSample (0, 0)));
            expect (std::isfinite (mono.getSample (0, 63)));
        }

        beginTest ("a steady parameter does not hand pump's engine new settings every block");
        {
            pb::PumpTuneAdapter adapter;
            adapter.prepare (kRate, 64, 2);
            const auto params = adapter.params();

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();

            adapter.process (buffer);
            const int afterFirst = adapter.settingsPushCount();

            for (int block = 0; block < 50; ++block)
            {
                // exactly what Slot::process does: every parameter pushed every block
                for (size_t i = 0; i < params.size(); ++i)
                    adapter.setParam ((int) i, params[i].defaultValue);

                adapter.process (buffer);
            }

            expectEquals (adapter.settingsPushCount(), afterFirst);

            adapter.setParam (indexOf (adapter, "retune"), 120.0f);          // one real change: exactly one push
            adapter.setParam (indexOf (adapter, "retune"), 120.0f);
            adapter.process (buffer);
            adapter.process (buffer);
            expectEquals (adapter.settingsPushCount(), afterFirst + 1);
        }

#ifdef PB_ALLOCATION_GUARD
        beginTest ("process and setParam allocate nothing");
        {
            pb::PumpTuneAdapter adapter;
            adapter.prepare (kRate, 128, 2);
            adapter.setParam (indexOf (adapter, "harm_mode"), 1.0f);
            const auto params = adapter.params();
            juce::AudioBuffer<float> buffer (2, 128);
            juce::Random rng (0x9u);

            for (int block = 0; block < 12; ++block)
            {
                for (int i = 0; i < 128; ++i)
                    buffer.setSample (0, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));

                buffer.copyFrom (1, 0, buffer, 0, 0, 128);
                pb::alloc::resetViolations();

                {
                    const pb::alloc::Scope scope;

                    for (size_t i = 0; i < params.size(); ++i)
                        adapter.setParam ((int) i, block % 2 == 0 ? params[i].defaultValue : 0.5f * (params[i].minValue + params[i].maxValue));

                    adapter.process (buffer);
                }

                expectEquals (pb::alloc::violations(), 0);
            }
        }
#endif

        for (const int blockSize : { 64, 128, 511 })
        {
            beginTest ("tune null at " + juce::String (blockSize) + " samples, default settings");
            {
                const auto r = runNull (blockSize, [] (pb::PumpTuneAdapter&) {});
                logMessage ("tune, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f, "adapter and pump's engine must be bit-identical");
                expect (r.changedFromInput > 0.01f, "the pedal really did process the signal");
            }

            beginTest ("tune null at " + juce::String (blockSize) + " samples, hard tune to A minor with harmonies");
            {
                const auto r = runNull (blockSize, [] (pb::PumpTuneAdapter& adapter)
                {
                    adapter.setParam (indexOf (adapter, "key"), 9.0f);             // A
                    adapter.setParam (indexOf (adapter, "scale"), 2.0f);           // minor
                    adapter.setParam (indexOf (adapter, "retune"), 0.0f);          // hard
                    adapter.setParam (indexOf (adapter, "humanize"), 20.0f);
                    adapter.setParam (indexOf (adapter, "vib_depth"), 30.0f);
                    adapter.setParam (indexOf (adapter, "formant"), 2.0f);
                    adapter.setParam (indexOf (adapter, "harm_mode"), 1.0f);       // scale steps
                    adapter.setParam (indexOf (adapter, "h3_level"), 50.0f);
                    adapter.setParam (indexOf (adapter, "harm_level"), -3.0f);
                    adapter.setParam (indexOf (adapter, "out_gain"), -2.0f);
                });

                logMessage ("tune, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }

            beginTest ("tune null at " + juce::String (blockSize) + " samples, fixed octave doubles and chipmunk formants");
            {
                const auto r = runNull (blockSize, [] (pb::PumpTuneAdapter& adapter)
                {
                    adapter.setParam (indexOf (adapter, "transpose"), 7.0f);
                    adapter.setParam (indexOf (adapter, "formant_follow"), 1.0f);
                    adapter.setParam (indexOf (adapter, "harm_mode"), 2.0f);       // fixed semitones
                    adapter.setParam (indexOf (adapter, "h1"), 12.0f);
                    adapter.setParam (indexOf (adapter, "h2"), -12.0f);
                    adapter.setParam (indexOf (adapter, "lead"), 80.0f);
                    adapter.setParam (indexOf (adapter, "harm_formant"), -3.0f);
                });

                logMessage ("tune, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }
        }
    }
};

static TuneNullTests tuneNullTests;
