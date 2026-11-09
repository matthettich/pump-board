// Regression tests for the 8 Nov 2026 audit (docs/NOTES.md, "audit of main"). One scenario per fix.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/AllocationGuard.h"
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/MidiClock.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Presets/BoardState.h"
#include "Presets/PresetManager.h"

namespace
{
pb::ParameterRegistry::Entry toggleEntry (const juce::String& id)
{
    pb::ParameterRegistry::Entry e;
    e.id = id;
    e.minValue = 0.0f;
    e.maxValue = 1.0f;
    e.defaultValue = 0.0f;
    e.isToggle = true;
    return e;
}

std::unique_ptr<pb::MappingTable> noteToggle (int paramIndex, int note)
{
    auto table = std::make_unique<pb::MappingTable>();
    pb::Mapping mapping;
    mapping.source = { pb::SourceType::Note, 0, note };
    mapping.action = pb::ActionType::Toggle;
    mapping.paramIndex = paramIndex;
    table->mappings.push_back (mapping);
    return table;
}
} // namespace

class AuditFixesTests final : public juce::UnitTest
{
public:
    AuditFixesTests() : juce::UnitTest ("AuditFixes", "engine") {}

    void runTest() override
    {
        beginTest ("the clock writes the tempo through the index it is given, with no lookup by name");
        {
            // The audio thread must not call ParameterRegistry::find (a string compare over the whole table, racing
            // the message thread's add/remove, and the literal builds a String on the heap). The processor hands the
            // clock its own tempo index; here the param's id is not "board/tempo", so a lookup by name would find nothing.
            pb::ParameterRegistry registry;
            registry.add (toggleEntry ("unrelated"));

            pb::ParameterRegistry::Entry tempo;
            tempo.id = "not-the-tempo";
            tempo.minValue = 20.0f;
            tempo.maxValue = 300.0f;
            tempo.defaultValue = 120.0f;
            const int tempoIndex = registry.add (tempo);

            pb::clock::reset();

            int pos = 0;

            for (int block = 0; block < 3000; ++block)       // 240 BPM: a pulse every 500 samples
            {
                juce::MidiBuffer midi;

                for (int i = 0; i < 64; ++i)
                    if ((pos + i) % 500 == 0)
                        midi.addEvent (juce::MidiMessage::midiClock(), i);

                pb::clock::observe (midi, 64, registry, 48000.0, tempoIndex);
                pos += 64;
            }

            expectWithinAbsoluteError (registry.get (tempoIndex), 240.0f, 2.0f);
            pb::clock::reset();
        }

        beginTest ("a system-exclusive message in the block is skipped, and the note after it still maps");
        {
            // MidiMessageMetadata::getMessage() copies the bytes, and a message over four bytes is heap-allocated:
            // a controller's SysEx dump must be skipped by its size before it is ever turned into a MidiMessage.
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            const int toggle = registry.add (toggleEntry ("switch"));
            router.publish (noteToggle (toggle, 60));

            const juce::uint8 sysex[] = { 0xf0, 0x7e, 0x7f, 0x06, 0x02, 0x00, 0x20, 0x29, 0x01, 0x00, 0xf7 };

            juce::MidiBuffer midi;
            midi.addEvent (sysex, (int) sizeof (sysex), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1);

            router.handle (midi);
            expectEquals (registry.get (toggle), 1.0f);

            pb::clock::reset();
            pb::clock::observe (midi, 64, registry, 48000.0);    // the clock reads the same block
            expect (! pb::clock::isRunning());
        }

        beginTest ("with no device running, retired chains are freed at once rather than never");
        {
            // Blocks are what retire a snapshot, so with the device stopped (or never started) nothing ever advanced
            // the counter: every board load kept the old board's slots, and their params, alive for good.
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);
            pb::BoardModel model (processor, effects, registry);

            processor.prepareToPlay (48000.0, 64);
            expect (model.insert (0, "pump.tube"));

            const auto id = "slot/" + model.at (0)->getUid() + "/on";
            expect (registry.find (id) != pb::ParameterRegistry::kInvalid);

            processor.releaseResources();                     // the device stops
            model.clear();
            processor.collectGarbage();

            expect (registry.find (id) == pb::ParameterRegistry::kInvalid,
                    "the cleared slot is released although no block has run since");
        }

        beginTest ("a device that is running still holds a retired chain for two blocks");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);
            pb::BoardModel model (processor, effects, registry);

            processor.prepareToPlay (48000.0, 64);
            expect (model.insert (0, "pump.tube"));
            const auto id = "slot/" + model.at (0)->getUid() + "/on";

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            buffer.clear();
            processor.processBlock (buffer, midi);

            model.clear();
            processor.collectGarbage();
            expect (registry.find (id) != pb::ParameterRegistry::kInvalid, "still inside the two-block window");

            processor.processBlock (buffer, midi);
            processor.processBlock (buffer, midi);
            processor.collectGarbage();
            expect (registry.find (id) == pb::ParameterRegistry::kInvalid);
        }

        beginTest ("a mapping target whose slot is not a plain position is dropped, not aimed at slot 0");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);
            pb::BoardModel model (processor, effects, registry);
            pb::MacroBank macros (registry);
            pb::KeyRouter keys (router);
            pb::LearnController learn (registry, router, keys);
            processor.prepareToPlay (48000.0, 64);

            pb::BoardData data;
            data.slots.resize (2);
            data.slots[0].typeId = "pump.tube";
            data.slots[1].typeId = "pump.echo";

            const char* const targets[] = { "slot/s1/on", "slot/x/on", "slot/1x/on", "slot/4294967296/on", "slot/1/on" };

            for (const auto* target : targets)
            {
                pb::BoardData::MappingData mapping;
                mapping.source = "note";
                mapping.number = 60;
                mapping.action = "toggle";
                mapping.target = target;
                data.mappings.push_back (mapping);
            }

            const auto result = pb::apply (data, model, macros, learn, registry);

            expect (result.ok);
            expectEquals ((int) learn.mappings().size(), 1, "only the plain \"slot/1/on\" survives");
            expectEquals (result.warnings.size(), 4);

            if (learn.mappings().size() == 1)
                expectEquals (learn.mappings()[0].paramIndex, model.at (1)->onParamIndex());
        }

        beginTest ("a CC footswitch learned onto a switch defaults to the threshold, a note to Toggle");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::KeyRouter keys (router);
            pb::LearnController learn (registry, router, keys);
            const int toggle = registry.add (toggleEntry ("test/switch"));

            learn.beginLearn (toggle);
            learn.sourceCaptured ({ pb::SourceType::CC, 1, 64 });
            expect (learn.defaultActionFor (toggle) == pb::ActionType::CCThreshold);
            learn.commitPending (learn.defaultActionFor (toggle));

            juce::MidiBuffer down, up;
            down.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
            up.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);

            router.handle (down);
            expectEquals (registry.get (toggle), 1.0f, "the footswitch now does something");
            router.handle (up);
            expectEquals (registry.get (toggle), 0.0f);

            learn.beginLearn (toggle);
            learn.sourceCaptured ({ pb::SourceType::Note, 1, 36 });
            expect (learn.defaultActionFor (toggle) == pb::ActionType::Toggle);
            learn.cancelLearn();
        }

        beginTest ("a working board that cannot be read is set aside, not overwritten");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("pump-board-audit-aside");
            directory.deleteRecursively();
            directory.createDirectory();

            const auto working = directory.getChildFile ("current.board");
            expect (working.replaceWithText ("{ \"schemaVersion\": 99, \"slots\": [] }"));

            const auto kept = pb::setAsideUnreadable (working);

            expect (kept.existsAsFile());
            expectEquals (kept.loadFileAsString(), juce::String ("{ \"schemaVersion\": 99, \"slots\": [] }"));
            expect (! working.existsAsFile(), "the app is free to write a fresh working copy");

            // A second failure keeps its own copy rather than replacing the first.
            expect (working.replaceWithText ("not json"));
            const auto keptAgain = pb::setAsideUnreadable (working);
            expect (keptAgain.existsAsFile() && keptAgain != kept);
            expect (kept.existsAsFile());

            // Nothing there: nothing to keep.
            expect (pb::setAsideUnreadable (directory.getChildFile ("missing.board")) == juce::File());

            directory.deleteRecursively();
        }

#ifdef PB_ALLOCATION_GUARD
        beginTest ("every registered pedal runs a steady-state block, and a parameter move, without allocating");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);
            pb::BoardModel model (processor, effects, registry);
            processor.prepareToPlay (48000.0, 64);

            for (const auto& descriptor : effects.list())
            {
                model.clear();
                expect (model.insert (0, descriptor.typeId), descriptor.typeId);

                juce::AudioBuffer<float> buffer (2, 64);
                juce::MidiBuffer midi;
                int worst = 0;

                for (int block = 0; block < 12; ++block)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 64; ++i)
                            buffer.setSample (ch, i, 0.2f * std::sin (0.05f * (float) (i + 64 * block)));

                    // Move every parameter of the pedal a little each block: a setParam must not allocate either.
                    const auto& indices = model.at (0)->paramIndices();

                    for (const int index : indices)
                        if (index != pb::ParameterRegistry::kInvalid && block >= 6)
                            registry.setNormalised (index, registry.getNormalised (index) * 0.98f + 0.01f);

                    pb::alloc::resetViolations();
                    processor.processBlock (buffer, midi);

                    if (block >= 4)
                        worst = juce::jmax (worst, pb::alloc::violations());
                }

                expectEquals (worst, 0, descriptor.typeId + " allocated on the audio thread");
            }
        }
#endif
    }
};

static AuditFixesTests auditFixesTests;
