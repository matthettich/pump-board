#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Engine/BoardProcessor.h"

// Card 0.3's passthrough, tested without a device: what goes in comes out, trims apply, the limiter holds.
class BoardProcessorTests final : public juce::UnitTest
{
public:
    BoardProcessorTests() : juce::UnitTest ("BoardProcessor", "engine") {}

    static void fillConstant (juce::AudioBuffer<float>& buffer, float value)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, value);
    }

    void runTest() override
    {
        beginTest ("zero trims pass the signal through untouched");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.5f);

            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (buffer.getSample (1, 63), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (processor.inputPeak(), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (processor.outputPeak(), 0.5f, 1.0e-5f);
        }

        beginTest ("input trim scales before the chain");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);

            const int trim = registry.find ("board/inputTrim");
            expect (trim != pb::ParameterRegistry::kInvalid);
            registry.set (trim, -6.0f);

            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.5f);

            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.5f * 0.5011872f, 1.0e-4f);
        }

        beginTest ("the safety limiter holds the ceiling at -0.3 dBFS");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 1.0f);

            processor.processBlock (buffer, midi);

            const float ceiling = juce::Decibels::decibelsToGain (-0.3f);
            expectWithinAbsoluteError (buffer.getSample (0, 63), ceiling, 1.0e-4f);
            expect (processor.outputPeak() <= ceiling + 1.0e-5f);
        }

        beginTest ("a silent right channel is folded up from the left");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            buffer.clear();
            for (int i = 0; i < 64; ++i)
                buffer.setSample (0, i, 0.25f);

            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (1, 63), 0.25f, 1.0e-5f);
        }

        beginTest ("board bypass is inert while the chain is empty");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);

            const int bypass = registry.find ("board/bypass");
            expect (bypass != pb::ParameterRegistry::kInvalid);
            registry.set (bypass, 1.0f);

            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.4f);

            processor.processBlock (buffer, midi);

            // wet and dry are the same signal with no slots, so bypassing changes nothing yet
            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.4f, 1.0e-4f);
        }
    }
};

static BoardProcessorTests boardProcessorTests;
