#include "Plugin/PluginProcessor.h"
#include "Control/MidiClock.h"
#include "Effects/EffectRegistry.h"

namespace
{
class FixedTempoPlayHead final : public juce::AudioPlayHead
{
public:
    explicit FixedTempoPlayHead (double bpmIn) : bpmValue (bpmIn) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpmValue);
        return info;
    }

private:
    double bpmValue;
};

float tempoOf (pb::PluginSession& session)
{
    const int index = session.getRegistry().find ("board/tempo");
    return index != pb::ParameterRegistry::kInvalid ? session.getRegistry().get (index) : -1.0f;
}

// 24 clocks at one beat per 24000 samples, which is 120 BPM at 48 kHz.
juce::MidiBuffer clockAt120()
{
    juce::MidiBuffer clock;

    for (int i = 0; i < 25; ++i)
        clock.addEvent (juce::MidiMessage::midiClock(), i * 1000);

    return clock;
}

class PluginHostSyncTests final : public juce::UnitTest
{
public:
    PluginHostSyncTests() : juce::UnitTest ("PluginHostSync", "plugin") {}

    void runTest() override
    {
        ensureBuiltInEffects();

        beginTest ("the host's tempo becomes the board's tempo");
        {
            pb::PluginProcessor processor;
            FixedTempoPlayHead playHead (133.0);
            processor.setPlayHead (&playHead);
            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();

            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (tempoOf (processor.getSession()), 133.0f, 0.01f);

            processor.setPlayHead (nullptr);
        }

        beginTest ("a steady host tempo is written once, not on every block");
        {
            pb::PluginProcessor processor;
            FixedTempoPlayHead playHead (133.0);
            processor.setPlayHead (&playHead);
            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            juce::MidiBuffer midi;

            processor.processBlock (buffer, midi);                         // the first block carries the tempo in
            const auto counter = processor.getSession().getRegistry().changeCounter();

            for (int i = 0; i < 5; ++i)
                processor.processBlock (buffer, midi);

            // The registry's change counter is what the editor reads to mark a board as edited. A tempo written
            // again on every block moves it on every block, so a board just loaded from a preset was starred as
            // modified a few milliseconds later.
            expectEquals ((int) processor.getSession().getRegistry().changeCounter(), (int) counter);

            processor.setPlayHead (nullptr);
        }

        beginTest ("a running MIDI clock does not fight a host tempo");
        {
            pb::PluginProcessor processor;
            FixedTempoPlayHead playHead (90.0);
            processor.setPlayHead (&playHead);
            processor.prepareToPlay (48000.0, 25000);

            juce::AudioBuffer<float> buffer (2, 25000);
            buffer.clear();

            auto clock = clockAt120();
            processor.processBlock (buffer, clock);

            expectWithinAbsoluteError (tempoOf (processor.getSession()), 90.0f, 0.01f);

            processor.setPlayHead (nullptr);

        }

        beginTest ("with no host tempo, the MIDI clock still sets the tempo");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 25000);

            juce::AudioBuffer<float> buffer (2, 25000);
            buffer.clear();

            auto clock = clockAt120();
            processor.processBlock (buffer, clock);

            expectWithinAbsoluteError (tempoOf (processor.getSession()), 120.0f, 2.0f);
        }

        beginTest ("the reported latency is the chain's sum, and the tail is not zero");
        {
            pb::PluginProcessor processor;
            auto& session = processor.getSession();

            session.getModel().clear();
            session.getModel().insert (0, "pump.limit");
            session.getModel().insert (1, "pump.spring");
            processor.prepareToPlay (48000.0, 128);

            int sum = 0;

            for (int i = 0; i < session.getModel().size(); ++i)
                if (auto* slot = session.getModel().at (i))
                    if (auto* effect = slot->getEffect())
                        sum += juce::jmax (0, effect->latencySamples());

            expectEquals (processor.getLatencySamples(), sum);
            expectGreaterThan (processor.getTailLengthSeconds(), 0.0);
        }
    }

private:
    static void ensureBuiltInEffects()
    {
        auto& registry = pb::EffectRegistry::instance();

        if (! registry.contains ("pump.tube"))
            pb::registerBuiltInEffects (registry);
    }
};

PluginHostSyncTests pluginHostSyncTests;
} // namespace
