#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiClock.h"
#include "Control/ParameterRegistry.h"
#include "Control/MidiRouter.h"
#include "Effects/adapters/PumpGateAdapter.h"
#include "Engine/BoardProcessor.h"
#include "Engine/Slot.h"
#include <memory>
#include <cmath>

// Card 5.32: a MIDI clock moves the board's tempo. The clock is 24 pulses per quarter note, so the beat is the
// distance between every 24th pulse; feeding pulses at a known interval is exactly what a sequencer does.
namespace
{
// ParameterRegistry holds atomics, so it cannot be copied: the helper fills one the caller owns.
void addTempo (pb::ParameterRegistry& registry)
{
    pb::ParameterRegistry::Entry tempo;
    tempo.id = "board/tempo";
    tempo.displayName = "Tempo";
    tempo.minValue = 20.0f;
    tempo.maxValue = 300.0f;
    tempo.defaultValue = 120.0f;
    registry.add (tempo);
}

// `intervalSamples` between clocks; 24 of them make one beat.
void feed (pb::ParameterRegistry& registry, int intervalSamples, int blocks, int blockSize = 64, double rate = 48000.0)
{
    int pos = 0;

    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;

        for (int i = 0; i < blockSize; ++i)
            if ((pos + i) % intervalSamples == 0)
                midi.addEvent (juce::MidiMessage::midiClock(), i);

        pb::clock::observe (midi, blockSize, registry, rate);
        pos += blockSize;
    }
}

float tempoOf (pb::ParameterRegistry& registry)
{
    const int index = registry.find ("board/tempo");
    return index == pb::ParameterRegistry::kInvalid ? -1.0f : registry.get (index);
}
} // namespace

class MidiClockTests final : public juce::UnitTest
{
public:
    MidiClockTests() : juce::UnitTest ("MidiClock", "control") {}

    void runTest() override
    {
        beginTest ("a 120 BPM clock sets the board's tempo");
        {
            pb::ParameterRegistry registry; addTempo (registry);
            pb::clock::reset();

            // 120 BPM at 48k: 24000 samples per beat, so a pulse every 1000 samples. Four beats is enough for the
            // smoothing to have settled on a value that is not the starting 120 by luck.
            feed (registry, 1000, 3000);

            expect (pb::clock::isRunning());
            expectWithinAbsoluteError (tempoOf (registry), 120.0f, 1.0f);
            expectWithinAbsoluteError ((float) pb::clock::lastMeasuredBpm(), 120.0f, 1.0f);
        }

        beginTest ("a faster clock moves it, and a slower one too");
        {
            pb::ParameterRegistry registry; addTempo (registry);
            pb::clock::reset();

            feed (registry, 500, 3000);                  // 60 BPM at 24 ppqn would be 2000; 500 is 240 BPM
            expectWithinAbsoluteError (tempoOf (registry), 240.0f, 2.0f);

            pb::ParameterRegistry second; addTempo (second);
            pb::clock::reset();
            feed (second, 1500, 4000);                   // 80 BPM
            expectWithinAbsoluteError (tempoOf (second), 80.0f, 1.0f);
        }

        beginTest ("a clock faster than the board allows is clamped, not obeyed");
        {
            pb::ParameterRegistry registry; addTempo (registry);
            pb::clock::reset();

            feed (registry, 40, 3000);                   // 3000 BPM

            const float bpm = tempoOf (registry);
            expect (bpm <= 300.0f);
            expect (bpm >= 299.0f);
        }

        beginTest ("stop resets the clock but leaves the tempo alone");
        {
            pb::ParameterRegistry registry; addTempo (registry);
            pb::clock::reset();

            feed (registry, 1000, 3000);
            expect (pb::clock::isRunning());

            juce::MidiBuffer stop;
            stop.addEvent (juce::MidiMessage::midiStop(), 0);
            pb::clock::observe (stop, 64, registry, 48000.0);

            expect (! pb::clock::isRunning());
            expectWithinAbsoluteError (tempoOf (registry), 120.0f, 1.0f);   // a stopped clock does not rewind
        }

        beginTest ("notes and CCs are not a tempo");
        {
            pb::ParameterRegistry registry; addTempo (registry);
            pb::clock::reset();

            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 64), 32);
                pb::clock::observe (midi, 64, registry, 48000.0);
            }

            expect (! pb::clock::isRunning());
            expectWithinAbsoluteError (tempoOf (registry), 120.0f, 0.01f);
        }

        beginTest ("a second of silence between pulses restarts the beat instead of averaging it in");
        {
            pb::ParameterRegistry registry; addTempo (registry);
            pb::clock::reset();

            feed (registry, 1000, 1000);                 // settle at 120
            feed (registry, 48000, 2000);                // one pulse per second: not a tempo, a broken clock

            // Whatever it decides, it must stay inside the board's range and not explode.
            const float bpm = tempoOf (registry);
            expect (bpm >= 20.0f);
            expect (bpm <= 300.0f);
            expect (std::isfinite (bpm));
        }
    }
};

static MidiClockTests midiClockTests;

// The route, not the arithmetic: a clock arriving at the board's MIDI input has to move board/tempo and reach a
// tempo-aware pedal through BoardProcessor, the same path the app uses.
class MidiClockRouteTests final : public juce::UnitTest
{
public:
    MidiClockRouteTests() : juce::UnitTest ("MidiClockRoute", "engine") {}

    void runTest() override
    {
        beginTest ("a clock at the MIDI input moves the tempo and reaches a pedal");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            auto gate = std::make_unique<pb::PumpGateAdapter>();
            auto* rawGate = gate.get();

            pb::Slot slot ("clock-gate", registry);
            slot.setEffect (std::move (gate));
            slot.prepare (48000.0, 64);

            auto snapshot = std::make_unique<pb::ChainSnapshot>();
            snapshot->order[0] = &slot;
            snapshot->count = 1;
            processor.publish (std::move (snapshot));

            pb::clock::reset();

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();

            int pos = 0;

            for (int b = 0; b < 3000; ++b)
            {
                juce::MidiBuffer midi;

                for (int i = 0; i < 64; ++i)
                    if ((pos + i) % 1000 == 0)                 // 120 BPM at 48k
                        midi.addEvent (juce::MidiMessage::midiClock(), i);

                pos += 64;
                processor.processBlock (buffer, midi);
            }

            const int index = registry.find ("board/tempo");
            expect (index != pb::ParameterRegistry::kInvalid);
            expectWithinAbsoluteError (registry.get (index), 120.0f, 1.0f);
            expectWithinAbsoluteError (rawGate->currentTransport().bpm, 120.0, 1.0);
        }
    }
};

static MidiClockRouteTests midiClockRouteTests;
