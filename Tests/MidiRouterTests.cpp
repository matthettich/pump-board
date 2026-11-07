#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include <memory>
#include <vector>

namespace
{
struct Fixture
{
    pb::ParameterRegistry registry;
    pb::MidiRouter router { registry };

    int toggleIndex = pb::ParameterRegistry::kInvalid;
    int secondIndex = pb::ParameterRegistry::kInvalid;
    int valueIndex = pb::ParameterRegistry::kInvalid;

    Fixture()
    {
        toggleIndex = addEntry ("test/toggle", 0.0f, 1.0f, 0.0f, true);
        secondIndex = addEntry ("test/second", 0.0f, 1.0f, 0.0f, true);
        valueIndex = addEntry ("test/value", -10.0f, 10.0f, 0.0f, false);
    }

    int addEntry (const juce::String& id, float lo, float hi, float def, bool isToggle)
    {
        pb::ParameterRegistry::Entry entry;
        entry.id = id;
        entry.displayName = id;
        entry.minValue = lo;
        entry.maxValue = hi;
        entry.defaultValue = def;
        entry.isToggle = isToggle;
        return registry.add (entry);
    }

    float get (int index) const { return registry.get (index); }

    void publish (std::vector<pb::Mapping> mappings)
    {
        auto table = std::make_unique<pb::MappingTable>();
        table->mappings = std::move (mappings);
        router.publish (std::move (table));
    }

    void note (int channel, int number, int velocity)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (channel, number, (juce::uint8) velocity), 0);
        router.handle (midi);
    }

    void noteOff (int channel, int number)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOff (channel, number), 0);
        router.handle (midi);
    }

    void cc (int channel, int number, int value)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (channel, number, value), 0);
        router.handle (midi);
    }

    void program (int channel, int number)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::programChange (channel, number), 0);
        router.handle (midi);
    }

    std::vector<pb::ControlEvent> drain()
    {
        std::vector<pb::ControlEvent> out;
        router.drainEvents ([&out] (const pb::ControlEvent& event) { out.push_back (event); });
        return out;
    }
};

pb::Mapping mappingFor (pb::SourceType type, int channel, int number,
                        pb::ActionType action, int param,
                        float lo = 0.0f, float hi = 1.0f)
{
    pb::Mapping mapping;
    mapping.source = { type, (uint8_t) channel, number };
    mapping.action = action;
    mapping.paramIndex = param;
    mapping.minValue = lo;
    mapping.maxValue = hi;
    return mapping;
}

pb::Mapping noteMapping (int channel, int number, pb::ActionType action, int param,
                         float lo = 0.0f, float hi = 1.0f)
{
    return mappingFor (pb::SourceType::Note, channel, number, action, param, lo, hi);
}

pb::Mapping ccMapping (int number, pb::ActionType action, int param, float lo = 0.0f, float hi = 1.0f)
{
    return mappingFor (pb::SourceType::CC, 1, number, action, param, lo, hi);
}

pb::Mapping programMapping (int number, pb::ActionType action)
{
    return mappingFor (pb::SourceType::ProgramChange, 0, number, action, pb::ParameterRegistry::kInvalid);
}
} // namespace

class MidiRouterTests final : public juce::UnitTest
{
public:
    MidiRouterTests() : juce::UnitTest ("MidiRouter", "control") {}

    void runTest() override
    {
        beginTest ("a note mapped to Toggle flips a switch, and only a note-on flips it");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.toggleIndex) });

            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);

            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);

            // A release must not flip it back: note-off and note-on with velocity 0 are both releases.
            f.note (1, 60, 100);
            f.noteOff (1, 60);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);

            f.note (1, 60, 0);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);

            // A note the table does not name changes nothing.
            f.note (1, 61, 100);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);
        }

        beginTest ("Momentary holds the mapping's maximum while the note is down and returns to its minimum");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 62, pb::ActionType::Momentary, f.valueIndex, 0.2f, 0.9f) });

            expectWithinAbsoluteError (f.get (f.valueIndex), 0.0f, 1.0e-6f);   // the param's own default

            f.note (1, 62, 100);
            expectWithinAbsoluteError (f.get (f.valueIndex), 0.9f, 1.0e-6f);

            f.noteOff (1, 62);
            expectWithinAbsoluteError (f.get (f.valueIndex), 0.2f, 1.0e-6f);

            f.note (1, 62, 100);
            f.note (1, 62, 0);
            expectWithinAbsoluteError (f.get (f.valueIndex), 0.2f, 1.0e-6f);
        }

        beginTest ("CCThreshold puts 64 on the on side of the line");
        {
            Fixture f;
            f.publish ({ ccMapping (7, pb::ActionType::CCThreshold, f.valueIndex, -5.0f, 5.0f) });

            f.cc (1, 7, 64);
            expectWithinAbsoluteError (f.get (f.valueIndex), 5.0f, 1.0e-6f);

            f.cc (1, 7, 63);
            expectWithinAbsoluteError (f.get (f.valueIndex), -5.0f, 1.0e-6f);

            f.cc (1, 7, 127);
            expectWithinAbsoluteError (f.get (f.valueIndex), 5.0f, 1.0e-6f);

            f.cc (1, 7, 0);
            expectWithinAbsoluteError (f.get (f.valueIndex), -5.0f, 1.0e-6f);
        }

        beginTest ("SetValue maps the whole CC range onto the mapping's range");
        {
            Fixture f;
            f.publish ({ ccMapping (11, pb::ActionType::SetValue, f.valueIndex, -10.0f, 10.0f) });

            f.cc (1, 11, 0);
            expectWithinAbsoluteError (f.get (f.valueIndex), -10.0f, 1.0e-6f);

            f.cc (1, 11, 127);
            expectWithinAbsoluteError (f.get (f.valueIndex), 10.0f, 1.0e-6f);

            f.cc (1, 11, 64);
            expectWithinAbsoluteError (f.get (f.valueIndex), -10.0f + 64.0f / 127.0f * 20.0f, 1.0e-4f);

            // SetValue reads a CC value, so a note's velocity is not used for it.
            f.cc (1, 11, 0);
            f.publish ({ noteMapping (1, 63, pb::ActionType::SetValue, f.valueIndex, -10.0f, 10.0f) });
            f.note (1, 63, 127);
            expectWithinAbsoluteError (f.get (f.valueIndex), -10.0f, 1.0e-6f);
        }

        beginTest ("channel 0 matches any channel, a numbered channel matches only its own");
        {
            Fixture f;
            f.publish ({ noteMapping (0, 64, pb::ActionType::Toggle, f.toggleIndex),
                         noteMapping (2, 65, pb::ActionType::Toggle, f.secondIndex) });

            f.note (9, 64, 100);                     // channel 0 mapping, message on 9
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);

            f.note (1, 65, 100);                     // channel 2 mapping, message on 1
            expectWithinAbsoluteError (f.get (f.secondIndex), 0.0f, 1.0e-6f);

            f.note (2, 65, 100);
            expectWithinAbsoluteError (f.get (f.secondIndex), 1.0f, 1.0e-6f);

            // A CC on the right number but another channel does not reach a channel-2 mapping either.
            f.cc (1, 65, 127);
            expectWithinAbsoluteError (f.get (f.secondIndex), 1.0f, 1.0e-6f);
        }

        beginTest ("every mapping that matches a message is applied");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.toggleIndex),
                         noteMapping (1, 60, pb::ActionType::Toggle, f.secondIndex) });

            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (f.get (f.secondIndex), 1.0f, 1.0e-6f);
        }

        beginTest ("preset actions become events for the message thread, and never write a parameter");
        {
            Fixture f;
            f.publish ({ noteMapping (10, 60, pb::ActionType::PresetNext, pb::ParameterRegistry::kInvalid),
                         noteMapping (10, 61, pb::ActionType::PresetPrev, pb::ParameterRegistry::kInvalid),
                         programMapping (3, pb::ActionType::PresetSelect) });

            f.note (10, 60, 100);
            f.noteOff (10, 60);                      // the release must not post a second event
            f.note (10, 61, 100);
            f.program (5, 3);                        // program 3 on any channel selects preset 3

            const auto events = f.drain();
            expectEquals ((int) events.size(), 3);

            if (events.size() == 3)
            {
                expect (events[0].kind == pb::ControlEvent::Kind::PresetNext);
                expectEquals ((int) events[0].source.type, (int) pb::SourceType::Note);
                expectEquals (events[0].source.number, 60);

                expect (events[1].kind == pb::ControlEvent::Kind::PresetPrev);
                expectEquals (events[1].source.number, 61);

                expect (events[2].kind == pb::ControlEvent::Kind::PresetSelect);
                expectEquals ((int) events[2].source.type, (int) pb::SourceType::ProgramChange);
                expectEquals (events[2].value, 3);
            }

            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);
        }

        beginTest ("learn captures the next note without acting on it, then routing resumes");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.toggleIndex) });

            f.router.beginLearn();
            f.note (3, 42, 111);

            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);

            const auto events = f.drain();
            expectEquals ((int) events.size(), 1);

            if (! events.empty())
            {
                expect (events[0].kind == pb::ControlEvent::Kind::LearnCaptured);
                expectEquals ((int) events[0].source.type, (int) pb::SourceType::Note);
                expectEquals ((int) events[0].source.channel, 3);
                expectEquals (events[0].source.number, 42);
                expectEquals (events[0].value, 111);
            }

            // Exactly one message was captured; the next one routes again.
            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);
            expect (f.drain().empty());
        }

        beginTest ("learn waits through a note-off and captures the next CC");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.toggleIndex) });

            f.router.beginLearn();
            f.noteOff (1, 60);                       // not a source: keep waiting, act on nothing

            expect (f.drain().empty());
            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);

            f.cc (1, 20, 77);

            const auto events = f.drain();
            expectEquals ((int) events.size(), 1);

            if (! events.empty())
            {
                expect (events[0].kind == pb::ControlEvent::Kind::LearnCaptured);
                expectEquals ((int) events[0].source.type, (int) pb::SourceType::CC);
                expectEquals (events[0].source.number, 20);
                expectEquals (events[0].value, 77);
            }

            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);
        }

        beginTest ("cancelLearn puts the router back to routing");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.toggleIndex) });

            f.router.beginLearn();
            f.router.cancelLearn();
            f.note (1, 60, 100);

            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);
            expect (f.drain().empty());
        }

        beginTest ("a full event FIFO drops events instead of blocking the audio thread");
        {
            Fixture f;
            f.publish ({ noteMapping (16, 70, pb::ActionType::PresetNext, pb::ParameterRegistry::kInvalid) });

            constexpr int kSent = 300;

            for (int i = 0; i < kSent; ++i)
                f.note (16, 70, 100);                // would block forever if the FIFO waited

            // juce::AbstractFifo can hold capacity - 1 items (full and empty have to be distinguishable).
            const auto first = f.drain();
            expectEquals ((int) first.size(), 255);

            for (const auto& event : first)
                expect (event.kind == pb::ControlEvent::Kind::PresetNext);

            expect (f.drain().empty());              // nothing held back, and nothing duplicated
        }

        beginTest ("a replaced table takes effect and the retired one is collected after two blocks");
        {
            Fixture f;
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.toggleIndex) });

            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);

            // Swap the table. The old one retires at the current block count.
            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, f.secondIndex) });

            // Collecting straight away must not free a table the audio thread could still be reading.
            f.router.collectGarbage();
            f.router.collectGarbage();

            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.secondIndex), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);

            f.note (1, 60, 100);
            expectWithinAbsoluteError (f.get (f.secondIndex), 0.0f, 1.0e-6f);

            f.router.collectGarbage();               // past N+2, so the retired table goes here
            f.note (1, 60, 100);                     // and the live table still works after the free

            expectWithinAbsoluteError (f.get (f.secondIndex), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (f.get (f.toggleIndex), 1.0f, 1.0e-6f);
        }

        beginTest ("no table, no target and an unmappable message are all harmless");
        {
            Fixture f;

            f.note (1, 60, 100);                     // no table published yet
            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);

            f.publish ({ noteMapping (1, 60, pb::ActionType::Toggle, pb::ParameterRegistry::kInvalid),
                         ccMapping (7, pb::ActionType::SetValue, pb::ParameterRegistry::kInvalid) });

            f.note (1, 60, 100);
            f.cc (1, 7, 100);

            expectWithinAbsoluteError (f.get (f.toggleIndex), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (f.get (f.valueIndex), 0.0f, 1.0e-6f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
            midi.addEvent (juce::MidiMessage::aftertouchChange (1, 60, 100), 0);
            f.router.handle (midi);

            expect (f.drain().empty());
        }
    }
};

static MidiRouterTests midiRouterTests;
