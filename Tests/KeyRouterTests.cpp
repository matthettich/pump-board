#include <juce_core/juce_core.h>
#include "Control/KeyRouter.h"
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
    pb::Slot slot { "key-test", registry };

    std::vector<int> learned;
    std::vector<pb::ControlEvent> presets;

    Fixture()
    {
        keys.onLearnCaptured = [this] (int keyCode) { learned.push_back (keyCode); };
        keys.onPresetAction = [this] (const pb::ControlEvent& event) { presets.push_back (event); };
    }

    float onValue() const { return registry.get (slot.onParamIndex()); }

    void publish (std::vector<pb::Mapping> mappings)
    {
        auto table = std::make_unique<pb::MappingTable>();
        table->mappings = std::move (mappings);
        router.publish (std::move (table));
    }

    // Anything the audio-thread FIFO has queued. Keys must never put anything here.
    int queuedEvents()
    {
        int count = 0;
        router.drainEvents ([&count] (const pb::ControlEvent&) { ++count; });
        return count;
    }
};

pb::Mapping keyMapping (int keyCode, pb::ActionType action, int param, float lo = 0.0f, float hi = 1.0f)
{
    pb::Mapping mapping;
    mapping.source = { pb::SourceType::Key, 0, keyCode };
    mapping.action = action;
    mapping.paramIndex = param;
    mapping.minValue = lo;
    mapping.maxValue = hi;
    return mapping;
}
} // namespace

class KeyRouterTests final : public juce::UnitTest
{
public:
    KeyRouterTests() : juce::UnitTest ("KeyRouter", "control") {}

    void runTest() override
    {
        beginTest ("a key mapped to Toggle flips a slot's switch, and auto-repeat does not flip it twice");
        {
            Fixture f;
            f.publish ({ keyMapping ('a', pb::ActionType::Toggle, f.slot.onParamIndex()) });

            // A slot starts switched on.
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            f.keys.keyDown ('a');
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            // Holding the key must not flip it again and again while the OS repeats it.
            f.keys.keyDown ('a');
            f.keys.keyDown ('a');
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            f.keys.keyUp ('a');
            f.keys.keyDown ('a');
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            f.keys.keyUp ('a');
        }

        beginTest ("Momentary holds the switch on only while its key is down");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);
            f.publish ({ keyMapping ('b', pb::ActionType::Momentary, f.slot.onParamIndex()) });

            f.keys.keyDown ('b');
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            // A repeated press while held changes nothing.
            f.keys.keyDown ('b');
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            f.keys.keyUp ('b');
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            // A release we never saw the press for must not move anything.
            f.keys.keyUp ('b');
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);
        }

        beginTest ("a held key survives a poll of the keyboard, and releases when it is really gone");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);
            f.publish ({ keyMapping ('c', pb::ActionType::Momentary, f.slot.onParamIndex()) });

            f.keys.keyDown ('c');
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            // Still physically down: the switch stays on.
            f.keys.pollReleasedKeys ([] (int) { return true; });
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);

            f.keys.pollReleasedKeys ([] (int) { return false; });
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            // And a poll with nothing held is harmless.
            f.keys.pollReleasedKeys ([] (int) { return false; });
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);
        }

        beginTest ("preset actions from keys come back directly, never through the audio FIFO");
        {
            Fixture f;
            f.publish ({ keyMapping ('n', pb::ActionType::PresetNext, pb::ParameterRegistry::kInvalid),
                         keyMapping ('m', pb::ActionType::PresetPrev, pb::ParameterRegistry::kInvalid),
                         keyMapping ('p', pb::ActionType::PresetSelect, pb::ParameterRegistry::kInvalid) });

            expect (f.keys.keyDown ('n'));            // consumed, so the window can let it go no further
            f.keys.keyUp ('n');                       // a release must not ask for the next board
            f.keys.keyDown ('m');
            f.keys.keyUp ('m');
            f.keys.keyDown ('p');

            expectEquals ((int) f.presets.size(), 3);

            if (f.presets.size() == 3)
            {
                expect (f.presets[0].kind == pb::ControlEvent::Kind::PresetNext);
                expect (f.presets[1].kind == pb::ControlEvent::Kind::PresetPrev);
                expect (f.presets[2].kind == pb::ControlEvent::Kind::PresetSelect);
                expectEquals ((int) f.presets[0].source.type, (int) pb::SourceType::Key);
                expectEquals (f.presets[0].source.number, (int) 'n');
                expectEquals (f.presets[2].value, 0);  // a key carries no program number
            }

            expectEquals (f.queuedEvents(), 0);
        }

        beginTest ("learn captures the next key and routes nothing while it waits");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);
            f.publish ({ keyMapping ('d', pb::ActionType::Toggle, f.slot.onParamIndex()) });

            f.keys.beginLearn();
            expect (f.keys.isLearning());
            expect (f.keys.keyDown ('d'));
            expect (! f.keys.isLearning());

            expectEquals ((int) f.learned.size(), 1);

            if (! f.learned.empty())
                expectEquals (f.learned[0], (int) 'd');

            // The captured key was not also routed.
            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);

            // The capturing press still counts as held, so release it before pressing again (a real
            // keyboard would send the release anyway, and auto-repeat would otherwise swallow it).
            f.keys.keyUp ('d');

            // Now the same key routes as usual.
            f.keys.keyDown ('d');
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);
            f.keys.keyUp ('d');

            // A second press while learning would have been captured, so nothing is left waiting.
            expect (! f.keys.isLearning());
        }

        beginTest ("cancelLearn puts keys back to routing, and a release never captures");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);
            f.publish ({ keyMapping ('e', pb::ActionType::Toggle, f.slot.onParamIndex()) });

            f.keys.beginLearn();
            f.keys.cancelLearn();
            f.keys.keyDown ('e');

            expect (f.learned.empty());
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);
            f.keys.keyUp ('e');

            // Learning again: a release (a key nobody pressed) must not be taken as the source.
            f.keys.beginLearn();
            f.keys.keyUp ('q');
            expect (f.keys.isLearning());
            expect (f.learned.empty());

            // A bare modifier change carries no key and must not be captured either.
            f.keys.keyDown (0);
            expect (f.keys.isLearning());
            expect (f.learned.empty());

            f.keys.keyDown ('q');
            expect (! f.keys.isLearning());
            expectEquals ((int) f.learned.size(), 1);
        }

        beginTest ("CCThreshold and SetValue read a CC value, so a key does nothing");
        {
            Fixture f;
            f.registry.set (f.slot.onParamIndex(), 0.0f);
            f.publish ({ keyMapping ('v', pb::ActionType::SetValue, f.slot.onParamIndex()),
                         keyMapping ('t', pb::ActionType::CCThreshold, f.slot.onParamIndex()) });

            expect (! f.keys.keyDown ('v'));
            expect (! f.keys.keyDown ('t'));

            expectWithinAbsoluteError (f.onValue(), 0.0f, 1.0e-6f);
            expectEquals (f.queuedEvents(), 0);

            f.keys.keyUp ('v');
            f.keys.keyUp ('t');
        }

        beginTest ("an unmapped key does nothing and is not swallowed");
        {
            Fixture f;
            expect (! f.keys.keyDown ('z'));
            expectWithinAbsoluteError (f.onValue(), 1.0f, 1.0e-6f);
            expectEquals (f.queuedEvents(), 0);
            expect (f.learned.empty());
            expect (f.presets.empty());
        }
    }
};

static KeyRouterTests keyRouterTests;
