#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <memory>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/adapters/PumpEchoAdapter.h"
#include "Effects/adapters/PumpSpringAdapter.h"
#include "Engine/BoardProcessor.h"
#include "Engine/Slot.h"

class TempoTests final : public juce::UnitTest
{
public:
    TempoTests() : juce::UnitTest ("Tempo", "engine") {}

    void runTest() override
    {
        beginTest ("the board has its own tempo, defaulting to 120");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);

            const int index = registry.find ("board/tempo");
            expect (index != pb::ParameterRegistry::kInvalid);

            if (index != pb::ParameterRegistry::kInvalid)
            {
                expectWithinAbsoluteError (registry.get (index), 120.0f, 1.0e-6f);
                expectEquals (registry.info (index).minValue, 20.0f);
                expectEquals (registry.info (index).maxValue, 300.0f);
            }
        }

        beginTest ("the board's tempo reaches the echo, and is clamped like any other value");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            auto echo = std::make_unique<pb::PumpEchoAdapter>();
            auto* rawEcho = echo.get();

            pb::Slot slot ("tempo-echo", registry);
            slot.setEffect (std::move (echo));
            slot.prepare (48000.0, 64);

            auto snapshot = std::make_unique<pb::ChainSnapshot>();
            snapshot->order[0] = &slot;
            snapshot->count = 1;
            processor.publish (std::move (snapshot));

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            const int tempo = registry.find ("board/tempo");

            buffer.clear();
            processor.processBlock (buffer, midi);
            expectWithinAbsoluteError (rawEcho->tempo(), 120.0, 1.0e-9);

            registry.set (tempo, 140.0f);
            buffer.clear();
            processor.processBlock (buffer, midi);
            expectWithinAbsoluteError (rawEcho->tempo(), 140.0, 1.0e-9);

            // The range is the param's, not the pedal's: 10 becomes 20, 999 becomes 300.
            registry.set (tempo, 10.0f);
            buffer.clear();
            processor.processBlock (buffer, midi);
            expectWithinAbsoluteError (rawEcho->tempo(), 20.0, 1.0e-9);

            registry.set (tempo, 999.0f);
            buffer.clear();
            processor.processBlock (buffer, midi);
            expectWithinAbsoluteError (rawEcho->tempo(), 300.0, 1.0e-9);
        }

        beginTest ("a pedal with no tempo is left alone, and the board still makes sound");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            auto spring = std::make_unique<pb::PumpSpringAdapter>();
            auto* rawSpring = spring.get();

            auto echo = std::make_unique<pb::PumpEchoAdapter>();
            auto* rawEcho = echo.get();

            pb::Slot springSlot ("tempo-spring", registry);
            springSlot.setEffect (std::move (spring));
            springSlot.prepare (48000.0, 64);

            pb::Slot echoSlot ("tempo-echo", registry);
            echoSlot.setEffect (std::move (echo));
            echoSlot.prepare (48000.0, 64);

            auto snapshot = std::make_unique<pb::ChainSnapshot>();
            snapshot->order[0] = &springSlot;
            snapshot->order[1] = &echoSlot;
            snapshot->count = 2;
            processor.publish (std::move (snapshot));

            registry.set (registry.find ("board/tempo"), 90.0f);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            for (int block = 0; block < 2; ++block)
            {
                buffer.clear();

                // A quiet signal in, so the spring has something to work with.
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i)
                        buffer.setSample (ch, i, 0.05f * std::sin (0.1f * (float) (i + block * 64)));

                processor.processBlock (buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i)
                        expect (std::isfinite (buffer.getSample (ch, i)));
            }

            expectWithinAbsoluteError (rawEcho->tempo(), 90.0, 1.0e-9);
            expect (rawSpring->displayName().isNotEmpty());
        }
    }
};

static TempoTests tempoTests;
