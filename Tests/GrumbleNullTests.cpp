#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/AllocationGuard.h"
#include "Effects/adapters/PumpGrumbleAdapter.h"
#include <cmath>
#include <functional>
#include <limits>

// pump GRUMBLE behind IEffect: the adapter and pump's own engine, fed the same signal with the same settings, must produce
// the same samples. GRUMBLE's noise (growl pulses, swarm grains) is seeded in reset(), so a mismatch is a real one.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;

// A sung vowel (about 150 Hz, vibrato, six harmonics) for the first 0.7 s, then a noise burst for the unvoiced path.
void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const double phase = twoPi * 150.0 * (double) t / kRate + 3.0 * std::sin (twoPi * 5.0 * (double) t / kRate);
        double v = 0.0;

        for (int k = 1; k <= 6; ++k)
            v += std::sin ((double) k * phase) / (double) k;

        float sample = 0.25f * (float) v;

        if (t > (int) (0.7 * kRate))
            sample = 0.15f * (rng.nextFloat() * 2.0f - 1.0f);

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
    float changedFromInput = 0.0f;
};

NullResult runNull (int blockSize, const std::function<void (pb::PumpGrumbleAdapter&)>& tweak)
{
    pb::PumpGrumbleAdapter adapter;
    adapter.prepare (kRate, blockSize, 2);
    tweak (adapter);
    adapter.reset();

    pump::grumble::Engine reference;
    reference.set (adapter.currentSettings());
    reference.prepare (kRate, blockSize);

    juce::Random adapterRng (0x6Bu), referenceRng (0x6Bu);
    NullResult result;

    for (int start = 0; start + blockSize <= kLength; start += blockSize)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize), input (2, blockSize);
        fillSignal (a, start, adapterRng);
        fillSignal (b, start, referenceRng);
        input.makeCopyOf (a);

        adapter.process (a);
        reference.process (b.getWritePointer (0), b.getWritePointer (1), blockSize);

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

class GrumbleNullTests final : public juce::UnitTest
{
public:
    GrumbleNullTests() : juce::UnitTest ("GrumbleNull", "effects") {}

    void runTest() override
    {
        beginTest ("pump GRUMBLE exposes pump's parameter layout, with the morph pad as two params");
        {
            pb::PumpGrumbleAdapter adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), 34);
            expectEquals (adapter.typeId(), juce::String ("pump.grumble"));
            expectEquals (adapter.displayName(), juce::String ("pump GRUMBLE"));
            expect (indexOf (adapter, "morph_x") >= 0 && indexOf (adapter, "morph_y") >= 0);
            expectEquals (indexOf (adapter, "bypass"), -1);

            expect (params[(size_t) indexOf (adapter, "beast_on")].isToggle);
            expectEquals (params[(size_t) indexOf (adapter, "beast_pitch")].defaultValue, -7.0f);
            expectEquals (params[(size_t) indexOf (adapter, "swarm_width")].defaultValue, 70.0f);
            expectEquals (params[(size_t) indexOf (adapter, "gate")].minValue, -90.0f);
            expectEquals (params[(size_t) indexOf (adapter, "gate")].defaultValue, -60.0f);
            expectEquals (params[(size_t) indexOf (adapter, "morph_x")].defaultValue, 50.0f);

            for (const auto& p : params)
                expect (p.defaultValue >= p.minValue && p.defaultValue <= p.maxValue, p.id + " default is in range");

            // the adapter's defaults are pump's Settings defaults
            const pump::grumble::Settings pumpDefaults;
            const auto& s = adapter.currentSettings();
            expect (s.on[0] == pumpDefaults.on[0] && s.on[1] == pumpDefaults.on[1] && s.on[2] == pumpDefaults.on[2] && s.on[3] == pumpDefaults.on[3]);
            expectEquals (s.beastPitch, pumpDefaults.beastPitch);
            expectEquals (s.growlSize, pumpDefaults.growlSize);
            expectEquals (s.swarmGrainMs, pumpDefaults.swarmGrainMs);
            expectEquals (s.gateDb, pumpDefaults.gateDb);
            expectEquals (s.cave, pumpDefaults.cave);
        }

        beginTest ("setParam fills pump's Settings the way PluginGrumble::sync() does");
        {
            pb::PumpGrumbleAdapter adapter;
            adapter.prepare (kRate, 128, 2);

            adapter.setParam (indexOf (adapter, "split_on"), 1.0f);
            adapter.setParam (indexOf (adapter, "beast_on"), 0.0f);
            adapter.setParam (indexOf (adapter, "swarm_width"), 33.0f);         // SWARM's "pan" is its width
            adapter.setParam (indexOf (adapter, "growl_pan"), -40.0f);
            adapter.setParam (indexOf (adapter, "swarm_level"), 25.0f);
            adapter.setParam (indexOf (adapter, "morph_x"), 10.0f);
            adapter.setParam (indexOf (adapter, "morph_y"), 90.0f);
            adapter.setParam (indexOf (adapter, "growl_size"), -9.0f);

            const auto& s = adapter.currentSettings();
            expect (s.on[pump::grumble::kSplit] && ! s.on[pump::grumble::kBeast]);
            expectEquals (s.pan[pump::grumble::kSwarm], 33.0f);
            expectEquals (s.pan[pump::grumble::kGrowl], -40.0f);
            expectEquals (s.level[pump::grumble::kSwarm], 25.0f);
            expectEquals (s.morphX, 10.0f);
            expectEquals (s.morphY, 90.0f);
            expectEquals (s.growlSize, -9.0f);

            adapter.setParam (indexOf (adapter, "drive"), 500.0f);               // clamped to pump's range
            expectEquals (adapter.currentSettings().drive, 24.0f);
            adapter.setParam (indexOf (adapter, "drive"), std::numeric_limits<float>::infinity());   // ignored
            expectEquals (adapter.currentSettings().drive, 24.0f);
        }

        beginTest ("pump GRUMBLE reports the resynthesis delay as latency");
        {
            pb::PumpGrumbleAdapter adapter;
            expectEquals (adapter.latencySamples(), 0);          // nothing to report before prepare

            adapter.prepare (kRate, 128, 2);

            pump::grumble::Engine reference;
            reference.prepare (kRate, 128);

            expectEquals (adapter.latencySamples(), reference.latency());
            expectWithinAbsoluteError ((double) adapter.latencySamples() / kRate * 1000.0, 24.0, 0.5);   // "about 24 ms"
        }

        beginTest ("a single-channel block is accepted and comes back finite");
        {
            pb::PumpGrumbleAdapter adapter;
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
            pb::PumpGrumbleAdapter adapter;
            adapter.prepare (kRate, 64, 2);
            const auto params = adapter.params();

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();

            adapter.process (buffer);
            const int afterFirst = adapter.settingsPushCount();

            for (int block = 0; block < 50; ++block)
            {
                for (size_t i = 0; i < params.size(); ++i)
                    adapter.setParam ((int) i, params[i].defaultValue);

                adapter.process (buffer);
            }

            expectEquals (adapter.settingsPushCount(), afterFirst);

            adapter.setParam (indexOf (adapter, "cave"), 60.0f);
            adapter.setParam (indexOf (adapter, "cave"), 60.0f);
            adapter.process (buffer);
            adapter.process (buffer);
            expectEquals (adapter.settingsPushCount(), afterFirst + 1);
        }

#ifdef PB_ALLOCATION_GUARD
        beginTest ("process and setParam allocate nothing");
        {
            pb::PumpGrumbleAdapter adapter;
            adapter.prepare (kRate, 128, 2);
            adapter.setParam (indexOf (adapter, "swarm_on"), 1.0f);
            adapter.setParam (indexOf (adapter, "split_on"), 1.0f);
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
            beginTest ("grumble null at " + juce::String (blockSize) + " samples, default settings");
            {
                const auto r = runNull (blockSize, [] (pb::PumpGrumbleAdapter&) {});
                logMessage ("grumble, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f, "adapter and pump's engine must be bit-identical");
                expect (r.changedFromInput > 0.01f, "the pedal really did process the signal");
            }

            beginTest ("grumble null at " + juce::String (blockSize) + " samples, all four layers, pad off-centre");
            {
                const auto r = runNull (blockSize, [] (pb::PumpGrumbleAdapter& adapter)
                {
                    adapter.setParam (indexOf (adapter, "split_on"), 1.0f);
                    adapter.setParam (indexOf (adapter, "swarm_on"), 1.0f);
                    adapter.setParam (indexOf (adapter, "morph_x"), 70.0f);
                    adapter.setParam (indexOf (adapter, "morph_y"), 30.0f);
                    adapter.setParam (indexOf (adapter, "beast_pan"), -50.0f);
                    adapter.setParam (indexOf (adapter, "split_wobble"), 60.0f);
                    adapter.setParam (indexOf (adapter, "growl_size"), -8.0f);
                    adapter.setParam (indexOf (adapter, "swarm_density"), 80.0f);
                    adapter.setParam (indexOf (adapter, "drive"), 10.0f);
                    adapter.setParam (indexOf (adapter, "tone"), -30.0f);
                    adapter.setParam (indexOf (adapter, "cave"), 40.0f);
                    adapter.setParam (indexOf (adapter, "mix"), 85.0f);
                    adapter.setParam (indexOf (adapter, "gate"), -90.0f);          // gate off
                });

                logMessage ("grumble, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }

            beginTest ("grumble null at " + juce::String (blockSize) + " samples, just a deeper beast");
            {
                const auto r = runNull (blockSize, [] (pb::PumpGrumbleAdapter& adapter)
                {
                    adapter.setParam (indexOf (adapter, "growl_on"), 0.0f);
                    adapter.setParam (indexOf (adapter, "beast_pitch"), -5.0f);
                    adapter.setParam (indexOf (adapter, "beast_fry"), 0.0f);
                    adapter.setParam (indexOf (adapter, "beast_level"), 100.0f);
                    adapter.setParam (indexOf (adapter, "drive"), 0.0f);
                    adapter.setParam (indexOf (adapter, "cave"), 0.0f);
                    adapter.setParam (indexOf (adapter, "out_gain"), -3.0f);
                });

                logMessage ("grumble, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }
        }
    }
};

static GrumbleNullTests grumbleNullTests;
