#include "Common/AllocationGuard.h"
#include "Effects/EffectRegistry.h"
#include "Plugin/PluginProcessor.h"

namespace
{
void ensureBuiltInEffects()
{
    auto& registry = pb::EffectRegistry::instance();

    if (! registry.contains ("pump.tube"))
        pb::registerBuiltInEffects (registry);
}

// Counts what the host would be told. One valueChanged per announcement, and nothing else.
class ChangeCounter final : public juce::AudioProcessorParameter::Listener
{
public:
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool) override {}
    int changes = 0;
};

// The processor's own 30 Hz timer does the board's message-thread work (macros, garbage, parameter polling), and
// nothing pumps timers while a test thread sleeps: run what is due until the condition holds, with a deadline.
template <typename Predicate>
bool pumpTimersUntil (Predicate&& condition, int timeoutMs = 2000)
{
    const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;

    for (;;)
    {
        juce::Timer::callPendingTimersSynchronously();

        if (condition())
            return true;

        if (juce::Time::currentTimeMillis() >= deadline)
            return false;

        juce::Thread::sleep (5);
    }
}

pb::PluginParameter* parameterFor (pb::PluginProcessor& processor, const juce::String& paramId)
{
    return processor.getAutomation().find (paramId);
}

float macroValue (pb::PluginProcessor& processor, int macro)
{
    auto& registry = processor.getSession().getRegistry();
    return registry.get (processor.getSession().getMacros().macroParamIndex (macro));
}

class PluginParameterTests final : public juce::UnitTest
{
public:
    PluginParameterTests() : juce::UnitTest ("PluginParameters", "plugin") {}

    void runTest() override
    {
        ensureBuiltInEffects();

        beginTest ("the automation surface is the set the plan chose");
        {
            pb::PluginProcessor processor;

            const juce::StringArray expected { "macro/0", "macro/1", "macro/2", "macro/3", "macro/4", "macro/5",
                                               "macro/6", "macro/7", "board/bypass", "board/inputTrim",
                                               "board/outputTrim", "slot/0/on", "slot/7/on", "slot/15/on" };

            for (const auto& id : expected)
                expect (parameterFor (processor, id) != nullptr, id);

            // Eight macros, three board controls and sixteen switches — and nothing per-pedal, which is the decision
            // the plan's "before card 8.1" section made and card 8.4 keeps.
            expectEquals (processor.getParameters().size(), 27);
        }

        beginTest ("automation written by the host reaches the board");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);

            auto* macro = parameterFor (processor, "macro/2");
            expect (macro != nullptr);
            macro->setValueNotifyingHost (0.4f);
            expectWithinAbsoluteError (macroValue (processor, 2), 0.4f, 1.0e-4f);

            auto* bypass = parameterFor (processor, "board/bypass");
            bypass->setValueNotifyingHost (1.0f);
            expect (processor.getSession().isBoardBypassed());

            bypass->setValueNotifyingHost (0.0f);
            expect (! processor.getSession().isBoardBypassed());
        }

        beginTest ("a change made in the board is announced to the host once");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);

            auto* macro = parameterFor (processor, "macro/1");
            ChangeCounter counter;
            macro->addListener (&counter);

            processor.getSession().getRegistry().set (processor.getSession().getMacros().macroParamIndex (1), 0.66f);

            processor.getAutomation().poll();
            processor.getAutomation().poll();
            processor.getAutomation().poll();

            expectEquals (counter.changes, 1);

            macro->removeListener (&counter);
        }

        beginTest ("a gesture the host started is not interrupted");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);

            auto* macro = parameterFor (processor, "macro/3");
            ChangeCounter counter;
            macro->addListener (&counter);

            auto& registry = processor.getSession().getRegistry();
            const int index = processor.getSession().getMacros().macroParamIndex (3);

            macro->beginChangeGesture();
            registry.set (index, 0.2f);
            processor.getAutomation().poll();
            expectEquals (counter.changes, 0);

            macro->endChangeGesture();
            registry.set (index, 0.9f);
            processor.getAutomation().poll();
            expectEquals (counter.changes, 1);

            macro->removeListener (&counter);
        }

        beginTest ("automation follows the slot, not the pedal");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);

            auto& model = processor.getSession().getModel();
            expectEquals (model.size(), 3);

            const int movedSwitch = model.at (2)->onParamIndex();     // the pedal sitting in position 2

            expect (model.insert (0, "pump.echo"));                   // put a pedal in front of it
            expectEquals (model.size(), 4);

            processor.getAutomation().refreshChain();

            auto* slotThree = parameterFor (processor, "slot/3/on");
            expect (slotThree != nullptr);
            expectEquals (model.at (3)->onParamIndex(), movedSwitch);      // the pedal kept its own switch
            expectEquals (slotThree->getRegistryIndex(), movedSwitch);     // and slot/3/on now names position 3

            slotThree->setValueNotifyingHost (0.0f);
            expect (processor.getSession().getRegistry().get (movedSwitch) < 0.5f);

            auto* slotZero = parameterFor (processor, "slot/0/on");
            expectEquals (slotZero->getRegistryIndex(), model.at (0)->onParamIndex());
            expect (slotZero->getRegistryIndex() != movedSwitch);
        }

        beginTest ("a macro moved by the host reaches its targets with no editor open");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);

            auto& session = processor.getSession();
            auto& registry = session.getRegistry();
            const auto& indices = session.getModel().at (0)->paramIndices();
            expect (! indices.empty());

            const int target = indices[0];
            const auto& info = registry.info (target);

            pb::MacroBank::Target macroTarget;
            macroTarget.paramIndex = target;
            macroTarget.minValue = info.minValue;
            macroTarget.maxValue = info.maxValue;
            session.getMacros().setTargets (0, { macroTarget });

            // Nothing here builds an editor, so nothing but the processor's own timer can run MacroBank::update().
            parameterFor (processor, "macro/0")->setValueNotifyingHost (1.0f);

            expect (pumpTimersUntil ([&] { return std::abs (registry.get (target) - info.maxValue) < 1.0e-3f; }),
                    "the macro moved but its target stayed at " + juce::String (registry.get (target)));
        }

        beginTest ("a full automation pass allocates nothing");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            juce::MidiBuffer midi;

            pb::alloc::resetViolations();

            {
                // The guard is real only where the allocation hooks are compiled in (Debug); in Release the Scope is
                // an empty struct, so a plain unused local would warn. The test is worth running in both.
                [[maybe_unused]] const pb::alloc::Scope scope;

                for (auto* parameter : processor.getParameters())
                    parameter->setValue (0.5f);

                processor.processBlock (buffer, midi);
            }

            expectEquals (pb::alloc::violations(), 0);
        }
    }
};

PluginParameterTests pluginParameterTests;
} // namespace
