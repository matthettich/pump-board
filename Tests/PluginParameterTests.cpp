#include "Common/AllocationGuard.h"
#include "Effects/EffectRegistry.h"
#include "Plugin/PluginProcessor.h"
#include "Engine/Slot.h"
#include <cmath>

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

// Silence out, and a reported latency: whatever comes out of a bypassed board is only the delayed dry copy.
class LatentSilentFx final : public pb::IEffect
{
public:
    explicit LatentSilentFx (int latencyIn) : latency (latencyIn) {}

    juce::String typeId() const override { return "test.latent"; }
    juce::String displayName() const override { return "Test Latent"; }
    std::vector<pb::ParamInfo> params() const override { return {}; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.clear(); }
    int latencySamples() const noexcept override { return latency; }

private:
    int latency;
};

// Puts a silent, latent pedal alone on the processor's board, so wet is silence and dry is the (delayed) input.
std::unique_ptr<pb::Slot> publishSilentChain (pb::PluginProcessor& processor, int latency)
{
    auto slot = std::make_unique<pb::Slot> ("test-latent", processor.getSession().getRegistry());
    slot->setEffect (std::make_unique<LatentSilentFx> (latency));
    slot->prepare (48000.0, 64);

    auto snapshot = std::make_unique<pb::ChainSnapshot>();
    snapshot->order[0] = slot.get();
    snapshot->count = 1;
    processor.getSession().getProcessor().publish (std::move (snapshot));
    return slot;
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

        beginTest ("the host's bypass parameter is Board Bypass, with the id it always had");
        {
            pb::PluginProcessor processor;

            auto* hostBypass = processor.getBypassParameter();
            expect (hostBypass != nullptr);
            expect (hostBypass == parameterFor (processor, "board/bypass"));

            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (hostBypass))
                expectEquals (withId->paramID, juce::String ("board/bypass"));
            else
                expect (false, "the bypass parameter has no id");

            // One parameter with that id, so a host does not see a second Bypass beside it, and the list is the same
            // size as before.
            int withThatId = 0;

            for (auto* parameter : processor.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                    withThatId += withId->paramID == "board/bypass" ? 1 : 0;

            expectEquals (withThatId, 1);
            expectEquals (processor.getParameters().size(), 8 + 3 + pb::PluginParameters::kNumSlots);

            // Written the way a host writes it, it reaches the board's own switch.
            if (hostBypass != nullptr)
            {
                hostBypass->setValue (1.0f);
                expect (processor.getSession().isBoardBypassed());
                hostBypass->setValue (0.0f);
                expect (! processor.getSession().isBoardBypassed());
            }
        }

        beginTest ("a host bypass goes through the board's fade, not a cut");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);
            auto slot = publishSilentChain (processor, 0);
            auto* hostBypass = processor.getBypassParameter();
            expect (hostBypass != nullptr);            // the rest of this test is skipped, not crashed, without it

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            auto runBlock = [&] (float input)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i)
                        buffer.setSample (ch, i, input);

                processor.processBlock (buffer, midi);
            };

            for (int i = 0; i < 12; ++i)
                runBlock (0.5f);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.0f, 1.0e-3f);   // the wet path is silence

            if (hostBypass != nullptr) hostBypass->setValue (1.0f);               // the host's own bypass button
            runBlock (0.5f);

            // A cut to dry would put 0.5 here at once; the fade starts at the wet end.
            expectLessThan (buffer.getSample (0, 0), 0.05f);
            expectGreaterThan (buffer.getSample (0, 63), buffer.getSample (0, 0));

            for (int i = 0; i < 12; ++i)
                runBlock (0.5f);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.5f, 1.0e-3f);   // fully dry after ~10 ms

            // ...and back again the same way.
            if (hostBypass != nullptr) hostBypass->setValue (0.0f);
            runBlock (0.5f);
            expectGreaterThan (buffer.getSample (0, 0), 0.45f);
            expectLessThan (buffer.getSample (0, 63), buffer.getSample (0, 0));
        }

        beginTest ("a host-bypassed board still delays its dry signal by the chain's latency");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);
            constexpr int latency = 200;
            auto slot = publishSilentChain (processor, latency);
            auto* hostBypass = processor.getBypassParameter();
            expect (hostBypass != nullptr);

            if (hostBypass != nullptr)
                hostBypass->setValue (1.0f);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            for (int i = 0; i < 20; ++i)             // settle the fade
            {
                buffer.clear();
                processor.processBlock (buffer, midi);
            }

            int firstFrame = -1;

            for (int block = 0; block < 8 && firstFrame < 0; ++block)
            {
                buffer.clear();

                if (block == 0)
                {
                    buffer.setSample (0, 0, 0.5f);
                    buffer.setSample (1, 0, 0.5f);
                }

                processor.processBlock (buffer, midi);

                for (int i = 0; i < 64 && firstFrame < 0; ++i)
                    if (std::abs (buffer.getSample (0, i)) > 0.25f)
                        firstFrame = block * 64 + i;
            }

            // The wrapper's default cut-to-dry would return it at frame 0, one block late for every other pedal.
            expectEquals (firstFrame, latency);
        }

        beginTest ("a restore under a host bypass leaves it bypassed, and does not announce the fade");
        {
            pb::PluginProcessor source;
            source.getSession().getMacros().setName (5, "HOSTBYP");

            juce::MemoryBlock saved;
            source.getStateInformation (saved);

            pb::PluginProcessor live;
            live.prepareToPlay (48000.0, 128);
            auto* hostBypass = live.getBypassParameter();
            expect (hostBypass != nullptr);

            if (hostBypass != nullptr)
                hostBypass->setValue (1.0f);            // the host's bypass is already on

            ChangeCounter counter;

            if (hostBypass != nullptr)
                hostBypass->addListener (&counter);

            live.setStateInformation (saved.getData(), (int) saved.getSize());

            expect (pumpTimersUntil ([&live] { return live.getSession().getMacros().getName (5) == "HOSTBYP"; }));
            live.getAutomation().poll();

            expect (live.getSession().isBoardBypassed());          // the restore did not take the host's bypass off
            expectEquals (hostBypass != nullptr ? hostBypass->getValue() : -1.0f, 1.0f);
            expectEquals (counter.changes, 0);

            if (hostBypass != nullptr)
                hostBypass->removeListener (&counter);
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

        beginTest ("the trims are shown in decibels and the macros as percentages");
        {
            pb::PluginProcessor processor;

            // Half way along -24..+12 dB is -6 dB; a host's generic editor prints exactly this text.
            for (const char* id : { "board/inputTrim", "board/outputTrim" })
            {
                const auto text = parameterFor (processor, id)->getText (0.5f, 0);
                expect (text.endsWith ("dB"), juce::String (id) + " reads '" + text + "'");
            }

            expectEquals (parameterFor (processor, "macro/0")->getText (0.5f, 0), juce::String ("50%"));
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
