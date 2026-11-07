#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/AllocationGuard.h"
#include "Effects/adapters/PumpVoxAdapter.h"
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <vector>

// pump VOX behind IEffect. The vocoder (route "Synth carrier") and the VOICE FX engine are each compared with pump's own
// engine driven directly with the same settings: difference exactly 0. The "Held note" source plays one note by handing
// the engine the MIDI a held key would send, so the reference engine is given that same note-on.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kLength = 48000;

void fillSignal (juce::AudioBuffer<float>& buffer, int startSample, juce::Random& rng)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int t = startSample + i;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const double phase = twoPi * 140.0 * (double) t / kRate + 2.0 * std::sin (twoPi * 5.0 * (double) t / kRate);
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

void set (pb::PumpVoxAdapter& adapter, const char* id, float value)
{
    adapter.setParam (indexOf (adapter, id), value);
}

struct NullResult
{
    float worst = 0.0f;
    float changedFromInput = 0.0f;
    int latency = 0;
};

NullResult runNull (int blockSize, const std::function<void (pb::PumpVoxAdapter&)>& tweak)
{
    pb::PumpVoxAdapter adapter;
    adapter.prepare (kRate, blockSize, 2);
    tweak (adapter);
    adapter.reset();                     // takes the edited settings, structure included

    const bool fxEngine = adapter.activeEngineKind() == pump::vox::kEngineVoiceFx;
    const auto vocoderSettings = adapter.makeVocoderSettings();
    const bool held = vocoderSettings.source == pump::vox::kSourceMidi;

    pump::vox::Engine vocoder;
    pump::vox::VoiceFx voiceFx;

    if (fxEngine)
    {
        voiceFx.set (adapter.makeFxSettings());
        voiceFx.prepare (kRate);
    }
    else
    {
        vocoder.setSettings (vocoderSettings);
        vocoder.prepare (kRate, blockSize);
    }

    juce::Random adapterRng (0x5Au), referenceRng (0x5Au);
    NullResult result;
    result.latency = fxEngine ? voiceFx.latency() : vocoder.latency();
    std::vector<float> voice ((size_t) blockSize);
    bool firstBlock = true;

    for (int start = 0; start + blockSize <= kLength; start += blockSize)
    {
        juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize), input (2, blockSize);
        fillSignal (a, start, adapterRng);
        fillSignal (b, start, referenceRng);
        input.makeCopyOf (a);

        adapter.process (a);

        if (fxEngine)
        {
            for (int i = 0; i < blockSize; ++i)
                voice[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));

            voiceFx.process (voice.data(), b.getWritePointer (0), b.getWritePointer (1), blockSize);
        }
        else
        {
            // a held key: the note-on arrives at the first frame of the first block
            pump::vox::MidiMsg noteOn { 0u, 0x90, (uint8_t) adapter.paramValue (pb::PumpVoxAdapter::kNote), 100 };
            const bool send = held && firstBlock;

            vocoder.process (b.getWritePointer (0), b.getWritePointer (1), nullptr, nullptr,
                             b.getWritePointer (0), b.getWritePointer (1), blockSize, send ? &noteOn : nullptr, send ? 1 : 0);
        }

        firstBlock = false;

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

void feedLoud (pb::PumpVoxAdapter& adapter, int blocks, int blockSize, int& position, juce::Random& rng)
{
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        fillSignal (buffer, position, rng);
        position += blockSize;
        adapter.process (buffer);
    }
}

void feedSilence (pb::PumpVoxAdapter& adapter, int blocks, int blockSize)
{
    juce::AudioBuffer<float> buffer (2, blockSize);

    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();
        adapter.process (buffer);
    }
}
} // namespace

class VoxNullTests final : public juce::UnitTest
{
public:
    VoxNullTests() : juce::UnitTest ("VoxNull", "effects") {}

    void runTest() override
    {
        using A = pb::PumpVoxAdapter;

        beginTest ("pump VOX exposes pump's parameter layout, minus the sidechain routes");
        {
            A adapter;
            const auto params = adapter.params();

            expectEquals ((int) params.size(), (int) A::kNumParams);
            expectEquals (adapter.typeId(), juce::String ("pump.vox"));
            expectEquals (adapter.displayName(), juce::String ("pump VOX"));

            std::set<juce::String> ids;

            for (const auto& p : params)
            {
                expect (p.id.isNotEmpty() && p.name.isNotEmpty(), "every table row is filled in");
                expect (ids.insert (p.id).second, p.id + " is unique");
                expect (p.defaultValue >= p.minValue && p.defaultValue <= p.maxValue, p.id + " default is in range");

                if (p.choiceCount() > 0)
                    expectEquals (p.choiceCount(), (int) (p.maxValue - p.minValue) + 1, p.id + " has a name per value");
            }

            expectEquals (indexOf (adapter, "route"), -1);       // no sidechain, no split in a slot
            expectEquals (indexOf (adapter, "bypass"), -1);
            expectEquals (indexOf (adapter, "fx_note"), -1);     // the one `note` knob serves both engines

            expectEquals (params[(size_t) indexOf (adapter, "engine")].choiceLabel (1.0f), juce::String ("Voice FX"));
            expectEquals (params[(size_t) indexOf (adapter, "mode")].choiceLabel (2.0f), juce::String ("Talkbox"));
            expectEquals (params[(size_t) indexOf (adapter, "source")].choiceLabel (1.0f), juce::String ("Held note"));
            expectEquals (params[(size_t) indexOf (adapter, "fx_filter")].choiceLabel (3.0f), juce::String ("Megaphone"));
            expectEquals (params[(size_t) indexOf (adapter, "scale")].choiceLabel (0.0f), juce::String ("Exact"));
            expectEquals (params[(size_t) indexOf (adapter, "gate")].defaultValue, -90.0f);
            expectEquals (params[(size_t) indexOf (adapter, "note")].defaultValue, 45.0f);
            expectEquals (params[(size_t) indexOf (adapter, "fx_pitch_on")].defaultValue, 1.0f);

            // with every default, the adapter's vocoder settings are pump's own defaults
            const pump::vox::Settings pumpDefaults;
            const auto s = adapter.makeVocoderSettings();
            expectEquals (s.mode, pumpDefaults.mode);
            expectEquals (s.route, (int) pump::vox::kRouteSynth);
            expectEquals (s.bandsIndex, pumpDefaults.bandsIndex);
            expectEquals (s.lowHz, pumpDefaults.lowHz);
            expectEquals (s.highHz, pumpDefaults.highHz);
            expectEquals (s.tube, pumpDefaults.tube);
            expectEquals (s.attackMs, pumpDefaults.attackMs);
            expectEquals (s.gateDb, (double) pump::vox::kGateOff);
            expectEquals (s.scale, pumpDefaults.scale);
            expectEquals (s.glideMs, pumpDefaults.glideMs);
            expectEquals (s.detune, pumpDefaults.detune);
            expectEquals (s.spread, pumpDefaults.spread);
            expectEquals (s.source, (int) pump::vox::kSourceFollow);   // pump's own default is Auto; see the header
        }

        beginTest ("setParam fills pump's settings the way PluginVox does");
        {
            A adapter;
            adapter.prepare (kRate, 128, 2);

            set (adapter, "source", 1.0f);
            set (adapter, "note", 52.4f);
            set (adapter, "wave", 2.0f);
            set (adapter, "chord", 3.0f);
            set (adapter, "freeze", 1.0f);
            set (adapter, "fx_robot_on", 1.0f);
            set (adapter, "fx_filter", 2.0f);
            set (adapter, "fx_pitch", -7.0f);
            set (adapter, "out_gain", -4.0f);

            const auto v = adapter.makeVocoderSettings();
            expectEquals (v.source, (int) pump::vox::kSourceMidi);
            expectEquals (v.wave, 2);
            expectEquals (v.chord, 3);
            expect (v.freeze);
            expectEquals (v.outDb, -4.0);

            const auto f = adapter.makeFxSettings();
            expectEquals (f.note, 52);                          // the robot's note without MIDI
            expect (f.robotOn);
            expectEquals (f.filter, 2);
            expectEquals (f.pitch, -7.0f);
            expectEquals (f.outDb, -4.0f);                      // out_gain feeds both engines, as in pump's plugin

            set (adapter, "mode", 9.0f);                        // clamped
            adapter.setParam (indexOf (adapter, "detune"), std::numeric_limits<float>::quiet_NaN());   // ignored
            expectEquals (adapter.makeVocoderSettings().detune, 25.0);
        }

        beginTest ("the held note is played with the MIDI a held key would send");
        {
            A adapter;
            adapter.prepare (kRate, 64, 2);
            set (adapter, "source", 1.0f);
            set (adapter, "note", 48.0f);

            juce::Random rng (3u);
            int position = 0;

            expectEquals (adapter.heldNoteSent(), -1);
            feedLoud (adapter, 1, 64, position, rng);
            expectEquals (adapter.heldNoteSent(), -1);          // the engine is still on Follow: a note-on would be ignored
            feedLoud (adapter, 1, 64, position, rng);
            expectEquals (adapter.heldNoteSent(), 48);          // one block after the knob, the engine took the MIDI source

            set (adapter, "note", 55.0f);
            feedLoud (adapter, 1, 64, position, rng);
            expectEquals (adapter.heldNoteSent(), 55);

            const int pushes = adapter.settingsPushCount();
            feedLoud (adapter, 20, 64, position, rng);
            expectEquals (adapter.settingsPushCount(), pushes);  // nothing re-sent while the note stands

            // the carrier really sounds: noise as the voice, and the synth at a held note comes out
            float energy = 0.0f;

            for (int block = 0; block < 100; ++block)
            {
                juce::AudioBuffer<float> buffer (2, 64);

                for (int i = 0; i < 64; ++i)
                {
                    const float x = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x);
                }

                adapter.process (buffer);
                energy += buffer.getMagnitude (0, 64);
            }

            expect (energy > 0.5f, "the synth carrier plays the held note through the vocoder");

            set (adapter, "source", 0.0f);                      // back to Follow: a note-off
            feedLoud (adapter, 2, 64, position, rng);
            expectEquals (adapter.heldNoteSent(), -1);
        }

        beginTest ("engine, mode and FFT size wait for silence (the latency must not jump under a held note)");
        {
            A adapter;
            adapter.prepare (kRate, 64, 2);
            expectEquals (adapter.latencySamples(), 0);          // Classic: no latency
            expectEquals (adapter.activeEngineKind(), (int) pump::vox::kEngineVocoder);

            juce::Random rng (4u);
            int position = 0;

            set (adapter, "mode", 1.0f);                         // Spectral: adds the FFT size
            set (adapter, "engine", 1.0f);                       // and the voice FX: adds the PSOLA
            feedLoud (adapter, 200, 64, position, rng);          // 0.27 s of singing
            expectEquals (adapter.latencySamples(), 0);
            expectEquals (adapter.activeEngineKind(), (int) pump::vox::kEngineVocoder);

            feedSilence (adapter, 80, 64);                       // 0.1 s of quiet: the engine switch goes through
            expectEquals (adapter.activeEngineKind(), (int) pump::vox::kEngineVoiceFx);

            pump::vox::VoiceFx referenceFx;
            referenceFx.prepare (kRate);
            expectEquals (adapter.latencySamples(), referenceFx.latency());
            expectWithinAbsoluteError ((double) adapter.latencySamples() / kRate * 1000.0, 24.0, 1.0);   // pump: "about 24 ms"

            set (adapter, "engine", 0.0f);                       // back to the vocoder, now in Spectral mode
            feedLoud (adapter, 20, 64, position, rng);
            expectEquals (adapter.activeEngineKind(), (int) pump::vox::kEngineVoiceFx);   // still waiting
            feedSilence (adapter, 120, 64);                      // the mode's own 10 ms fade completes inside this

            pump::vox::Engine referenceVocoder;
            auto spectral = adapter.makeVocoderSettings();
            expectEquals (spectral.mode, (int) pump::vox::kSpectral);
            referenceVocoder.setSettings (spectral);
            referenceVocoder.prepare (kRate, 64);
            expect (referenceVocoder.latency() > 0, "Spectral has an FFT delay");
            expectEquals (adapter.latencySamples(), referenceVocoder.latency());

            // prepare() and reset() take the stored structure at once
            A fresh;
            set (fresh, "engine", 1.0f);
            fresh.prepare (kRate, 64, 2);
            expectEquals (fresh.latencySamples(), referenceFx.latency());
        }

        beginTest ("a steady parameter does not hand pump's engines new settings every block");
        {
            A adapter;
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

            set (adapter, "tube", 80.0f);
            set (adapter, "tube", 80.0f);
            adapter.process (buffer);
            adapter.process (buffer);
            expectEquals (adapter.settingsPushCount(), afterFirst + 1);
        }

        beginTest ("a single-channel block is accepted and comes back finite, in both engines");
        {
            for (const float engineKind : { 0.0f, 1.0f })
            {
                A adapter;
                set (adapter, "engine", engineKind);
                adapter.prepare (kRate, 64, 1);

                juce::AudioBuffer<float> mono (1, 64);
                juce::Random rng (0x1u);

                for (int block = 0; block < 20; ++block)
                {
                    for (int i = 0; i < 64; ++i)
                        mono.setSample (0, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));

                    adapter.process (mono);
                }

                expect (std::isfinite (mono.getSample (0, 0)) && std::isfinite (mono.getSample (0, 63)));
            }
        }

#ifdef PB_ALLOCATION_GUARD
        beginTest ("process and setParam allocate nothing, in either engine");
        {
            for (const float engineKind : { 0.0f, 1.0f })
            {
                A adapter;
                set (adapter, "engine", engineKind);
                set (adapter, "source", 1.0f);
                adapter.prepare (kRate, 128, 2);
                const auto params = adapter.params();
                juce::AudioBuffer<float> buffer (2, 128);
                juce::Random rng (0x9u);

                for (int block = 0; block < 12; ++block)
                {
                    for (int i = 0; i < 128; ++i)
                        buffer.setSample (0, i, block > 8 ? 0.0f : 0.2f * (rng.nextFloat() * 2.0f - 1.0f));

                    buffer.copyFrom (1, 0, buffer, 0, 0, 128);
                    pb::alloc::resetViolations();

                    {
                        const pb::alloc::Scope scope;

                        for (size_t i = 0; i < params.size(); ++i)
                            adapter.setParam ((int) i, block % 2 == 0 ? params[i].defaultValue : 0.5f * (params[i].minValue + params[i].maxValue));

                        adapter.process (buffer);       // the last blocks are silent, so a structural switch goes through here
                    }

                    expectEquals (pb::alloc::violations(), 0);
                }
            }
        }
#endif

        for (const int blockSize : { 64, 128, 511 })
        {
            const auto tag = [blockSize] (const char* what) { return "vox null at " + juce::String (blockSize) + " samples, " + what; };

            beginTest (tag ("vocoder, default settings"));
            {
                const auto r = runNull (blockSize, [] (A&) {});
                logMessage ("vox, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f, "adapter and pump's engine must be bit-identical");
                expect (r.changedFromInput > 0.01f, "the pedal really did process the signal");
            }

            beginTest (tag ("classic vocoder on a held note"));
            {
                const auto r = runNull (blockSize, [] (A& adapter)
                {
                    set (adapter, "source", 1.0f);
                    set (adapter, "note", 48.0f);
                    set (adapter, "wave", 1.0f);
                    set (adapter, "bands", 3.0f);
                    set (adapter, "ensemble", 30.0f);
                    set (adapter, "voice_dry", 20.0f);
                    set (adapter, "formant", 3.0f);
                });

                logMessage ("vox, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }

            beginTest (tag ("spectral vocoder following the voice in a scale, with a chord"));
            {
                const auto r = runNull (blockSize, [] (A& adapter)
                {
                    set (adapter, "mode", 1.0f);
                    set (adapter, "fft_size", 0.0f);
                    set (adapter, "chord", 3.0f);
                    set (adapter, "scale", 2.0f);
                    set (adapter, "key", 7.0f);
                    set (adapter, "wave", 2.0f);
                    set (adapter, "detail", 70.0f);
                });

                logMessage ("vox, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }

            beginTest (tag ("talkbox on a held note"));
            {
                const auto r = runNull (blockSize, [] (A& adapter)
                {
                    set (adapter, "mode", 2.0f);
                    set (adapter, "source", 1.0f);
                    set (adapter, "note", 40.0f);
                    set (adapter, "wave", 1.0f);
                    set (adapter, "tube", 50.0f);
                    set (adapter, "drive", 10.0f);
                });

                logMessage ("vox, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }

            beginTest (tag ("voice FX, default settings"));
            {
                const auto r = runNull (blockSize, [] (A& adapter) { set (adapter, "engine", 1.0f); });
                logMessage ("vox fx, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.latency > 0);
            }

            beginTest (tag ("voice FX, gravel robot at a set note"));
            {
                const auto r = runNull (blockSize, [] (A& adapter)
                {
                    set (adapter, "engine", 1.0f);
                    set (adapter, "fx_pitch", -5.0f);
                    set (adapter, "fx_formant", -3.0f);
                    set (adapter, "fx_grit_on", 1.0f);
                    set (adapter, "fx_robot_on", 1.0f);
                    set (adapter, "note", 50.0f);
                    set (adapter, "fx_mono", 90.0f);
                    set (adapter, "fx_ring", 30.0f);
                    set (adapter, "fx_char_on", 1.0f);
                    set (adapter, "fx_filter", 1.0f);
                    set (adapter, "fx_stack", 3.0f);
                    set (adapter, "fx_double", 30.0f);
                    set (adapter, "fx_slap", 25.0f);
                    set (adapter, "fx_mix", 90.0f);
                });

                logMessage ("vox fx, block " + juce::String (blockSize) + ": peak difference " + describe (r.worst));
                expect (r.worst <= 0.0f);
                expect (r.changedFromInput > 0.01f);
            }
        }
    }
};

static VoxNullTests voxNullTests;
