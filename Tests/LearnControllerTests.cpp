#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Engine/Slot.h"
#include <memory>
#include <vector>

namespace
{
struct Fixture
{
    pb::ParameterRegistry registry;
    pb::MidiRouter router { registry };
    pb::KeyRouter keys { router };
    pb::LearnController learn { registry, router, keys };
    pb::Slot slot { "learn-test", registry };

    bool askedForAction = false;
    std::vector<pb::ControlEvent> presetEvents;

    Fixture()
    {
        learn.onPendingSource = [this] { askedForAction = true; };
        learn.targetLabel = [this] (int paramIndex)
        {
            return paramIndex == slot.onParamIndex() ? juce::String ("TEST PEDAL SWITCH") : juce::String();
        };
    }

    float onValue() const { return registry.get (slot.onParamIndex()); }

    // What the window's 30 Hz timer does: drain the audio thread's events into the controller.
    void drain()
    {
        router.drainEvents ([this] (const pb::ControlEvent& event)
        {
            if (event.kind == pb::ControlEvent::Kind::LearnCaptured)
                learn.sourceCaptured (event.source);
            else
                presetEvents.push_back (event);
        });
    }

    void note (int channel, int number, int velocity)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (channel, number, (juce::uint8) velocity), 0);
        router.handle (midi);
        drain();
    }

    void cc (int channel, int number, int value)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (channel, number, value), 0);
        router.handle (midi);
        drain();
    }

    int queuedEvents()
    {
        int count = 0;
        router.drainEvents ([&count] (const pb::ControlEvent&) { ++count; });
        return count;
    }
};
} // namespace

class LearnControllerTests final : public juce::UnitTest
{
public:
    LearnControllerTests() : juce::UnitTest ("LearnController", "control") {}

    void runTest() override
    {
        beginTest ("learning a footswitch from a MIDI note maps it, and the note then flips it");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);

            f.learn.beginLearn (f.slot.onParamIndex());
            expect (f.learn.isLearning());
            expectEquals (f.learn.learningParamIndex(), f.slot.onParamIndex());

            f.note (4, 60, 100);                        // the learned message, captured not acted on

            expect (! f.learn.isLearning());
            expect (f.askedForAction);
            expect (f.learn.hasPendingSource());
            expectEquals ((int) f.learn.pendingSource().type, (int) pb::SourceType::Note);
            expectEquals (f.learn.pendingSource().number, 60);
            expectEquals ((int) f.learn.pendingSource().channel, 4);

            // Nothing was applied while learning, and nothing is mapped until the action is chosen.
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            f.learn.commitPending (f.learn.defaultActionFor (f.learn.pendingParamIndex()));

            expect (! f.learn.hasPendingSource());
            expectEquals ((int) f.learn.mappings().size(), 1);

            if (! f.learn.mappings().empty())
            {
                expect (f.learn.mappings()[0].action == pb::ActionType::Toggle);
                expectEquals (f.learn.mappings()[0].paramIndex, f.slot.onParamIndex());
                expectWithinAbsoluteError (f.learn.mappings()[0].minValue, 0.0f, 1.0e-6f);
                expectWithinAbsoluteError (f.learn.mappings()[0].maxValue, 1.0f, 1.0e-6f);
            }

            // Now the note is live, on every channel it was learned from.
            f.note (4, 60, 100);
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            f.note (4, 60, 100);
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            // And a different channel does not reach it.
            f.note (5, 60, 100);
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);
        }

        beginTest ("learning from a key maps that key, and the key then drives the control");
        {
            Fixture f;
            f.learn.beginLearn (f.slot.onParamIndex());
            expect (f.keys.isLearning());

            f.keys.keyDown ('k');                       // KeyRouter captures, the window forwards it

            expect (! f.keys.isLearning());
            expect (f.learn.hasPendingSource());
            expectEquals ((int) f.learn.pendingSource().type, (int) pb::SourceType::Key);
            expectEquals (f.learn.pendingSource().number, (int) 'k');

            f.learn.commitPending (pb::ActionType::Momentary);

            // The learned press was consumed, so release before testing the hold.
            f.keys.keyUp ('k');
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            f.keys.keyDown ('k');
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            f.keys.keyUp ('k');
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);
        }

        beginTest ("a switch defaults to Toggle and a knob to Value");
        {
            Fixture f;
            pb::ParameterRegistry::Entry entry;
            entry.id = "test/knob";
            entry.displayName = "Knob";
            entry.minValue = -10.0f;
            entry.maxValue = 10.0f;
            const int knob = f.registry.add (entry);

            expect (f.learn.defaultActionFor (f.slot.onParamIndex()) == pb::ActionType::Toggle);
            expect (f.learn.defaultActionFor (knob) == pb::ActionType::SetValue);

            // A Value mapping writes across the control's own range.
            f.learn.beginLearn (knob);
            f.cc (1, 7, 65);
            f.learn.commitPending (f.learn.defaultActionFor (knob));

            expectEquals ((int) f.learn.mappings().size(), 1);

            f.cc (1, 7, 127);
            expectWithinAbsoluteError (f.registry.get (knob), 10.0f, 1.0e-6f);

            f.cc (1, 7, 0);
            expectWithinAbsoluteError (f.registry.get (knob), -10.0f, 1.0e-6f);
        }

        beginTest ("one source can drive two targets, but learning the same pair twice replaces it");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);

            pb::ParameterRegistry::Entry entry;
            entry.id = "test/second";
            entry.displayName = "Second";
            const int second = f.registry.add (entry);

            // A note onto the switch…
            f.learn.beginLearn (f.slot.onParamIndex());
            f.note (1, 62, 100);
            f.learn.commitPending (pb::ActionType::Toggle);

            // …and the same note onto a second target: both may move from one footswitch.
            f.learn.beginLearn (second);
            f.note (1, 62, 100);
            f.learn.commitPending (pb::ActionType::Toggle);

            expectEquals ((int) f.learn.mappings().size(), 2);

            // Learning that same note onto the switch again must replace, not add a cancelling twin.
            f.learn.beginLearn (f.slot.onParamIndex());
            f.note (1, 62, 100);
            f.learn.commitPending (pb::ActionType::Toggle);

            expectEquals ((int) f.learn.mappings().size(), 2);

            f.note (1, 62, 100);
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);     // one flip, not two
            expectWithinAbsoluteError (f.registry.get (second), 1.0f, 1.0e-6f);
        }

        beginTest ("dismissing the action popup leaves nothing mapped");
        {
            Fixture f;
            f.learn.beginLearn (f.slot.onParamIndex());
            f.note (1, 64, 100);

            expect (f.learn.hasPendingSource());
            f.learn.discardPending();

            expect (! f.learn.hasPendingSource());
            expect (f.learn.mappings().empty());

            f.note (1, 64, 100);
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);     // unmapped: the slot's own default
        }

        beginTest ("Esc cancels learn, and the other source stops waiting too");
        {
            Fixture f;

            // Cancelling a MIDI learn must disarm the keyboard as well.
            f.learn.beginLearn (f.slot.onParamIndex());
            f.learn.cancelLearn();

            expect (! f.learn.isLearning());
            expect (! f.keys.isLearning());
            expect (f.learn.mappings().empty());

            // A cancelled learn routes normally instead of capturing.
            f.note (1, 65, 100);
            expect (! f.learn.hasPendingSource());
            expectEquals (f.queuedEvents(), 0);

            // And capturing from a key must disarm the MIDI router: a later note is not captured.
            f.learn.beginLearn (f.slot.onParamIndex());
            f.keys.keyDown ('j');
            expect (f.learn.hasPendingSource());
            f.learn.discardPending();
            f.keys.keyUp ('j');

            f.note (1, 66, 100);
            expect (! f.learn.hasPendingSource());
            expectEquals (f.queuedEvents(), 0);
        }

        beginTest ("the list deletes, re-actions and re-ranges a mapping, and the router follows");
        {
            Fixture f;
            pb::ParameterRegistry::Entry entry;
            entry.id = "test/knob";
            entry.displayName = "Knob";
            entry.minValue = -10.0f;
            entry.maxValue = 10.0f;
            const int knob = f.registry.add (entry);

            f.learn.beginLearn (knob);
            f.cc (1, 11, 10);
            f.learn.commitPending (pb::ActionType::SetValue);

            expectEquals ((int) f.learn.mappings().size(), 1);

            // A narrower range is honoured.
            f.learn.setMappingRange (0, -2.0f, 2.0f);
            f.cc (1, 11, 127);
            expectWithinAbsoluteError (f.registry.get (knob), 2.0f, 1.0e-6f);

            // Changing the action changes the behaviour: a CC switch now thresholds.
            f.learn.setMappingAction (0, pb::ActionType::CCThreshold);
            f.cc (1, 11, 127);
            expectWithinAbsoluteError (f.registry.get (knob), 2.0f, 1.0e-6f);   // max of the range
            f.cc (1, 11, 0);
            expectWithinAbsoluteError (f.registry.get (knob), -2.0f, 1.0e-6f);  // min of the range

            f.learn.removeMapping (0);
            expect (f.learn.mappings().empty());

            f.cc (1, 11, 127);
            expectWithinAbsoluteError (f.registry.get (knob), -2.0f, 1.0e-6f);  // table is empty again
        }

        beginTest ("the views get readable names for a source, an action and a target");
        {
            Fixture f;

            pb::MappingSource note { pb::SourceType::Note, 2, 60 };
            pb::MappingSource cc { pb::SourceType::CC, 0, 7 };
            pb::MappingSource key { pb::SourceType::Key, 0, (int) 'a' };

            expectEquals (f.learn.describeSource (note), juce::String ("NOTE 60  CH 2"));
            expectEquals (f.learn.describeSource (cc), juce::String ("CC 7  CH ANY"));
            expectEquals (f.learn.describeSource (key), juce::String ("KEY A"));
            expectEquals (f.learn.describeAction (pb::ActionType::SetValue), juce::String ("VALUE"));

            // The window's own label wins; without one, the param's display name is used.
            expectEquals (f.learn.describeTarget (f.slot.onParamIndex()), juce::String ("TEST PEDAL SWITCH"));

            f.learn.targetLabel = nullptr;
            expectEquals (f.learn.describeTarget (f.slot.onParamIndex()), juce::String ("ON"));
        }
    }
};

static LearnControllerTests learnControllerTests;
