#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Presets/BoardState.h"

namespace
{
class GainEffect final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "test.gain"; }
    juce::String displayName() const override { return "Test Gain"; }

    std::vector<pb::ParamInfo> params() const override
    {
        return { { "gain", "Gain", 0.0f, 4.0f, 2.0f, "", false, 0.0f } };
    }

    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int index, float value) noexcept override { if (index == 0) gain = value; }
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (gain); }

private:
    float gain = 2.0f;
};

class Gain3Effect final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "test.gain3"; }
    juce::String displayName() const override { return "Test Gain Three"; }

    std::vector<pb::ParamInfo> params() const override
    {
        return { { "gain", "Gain", 0.0f, 4.0f, 3.0f, "", false, 0.0f } };
    }

    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int index, float value) noexcept override { if (index == 0) gain = value; }
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (gain); }

private:
    float gain = 3.0f;
};

// Everything capture() and apply() touch, wired the way MainContent wires it.
struct Harness
{
    pb::ParameterRegistry registry;
    pb::MidiRouter router { registry };
    pb::BoardProcessor processor { registry, router };
    pb::EffectRegistry effects;
    pb::BoardModel model;
    pb::MacroBank macros;
    pb::KeyRouter keys { router };
    pb::LearnController learn;

    Harness()
        : model (processor, effects, registry),
          macros (registry),
          learn (registry, router, keys)
    {
        processor.prepareToPlay (48000.0, 64);

        effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<GainEffect>(); } });
        effects.add ({ "test.gain3", "Test Gain Three", "utility", [] { return std::make_unique<Gain3Effect>(); } });
    }

    static pb::Mapping noteMapping (int paramIndex, int note = 36)
    {
        pb::Mapping mapping;
        mapping.source = { pb::SourceType::Note, 0, note };
        mapping.action = pb::ActionType::Toggle;
        mapping.paramIndex = paramIndex;
        return mapping;
    }

    static pb::BoardData::SlotData slot (const juce::String& typeId, const juce::String& paramId = {},
                                         float value = 0.0f)
    {
        pb::BoardData::SlotData data;
        data.typeId = typeId;

        if (paramId.isNotEmpty())
            data.params.emplace_back (paramId, value);

        return data;
    }
};
} // namespace

class BoardStateTests final : public juce::UnitTest
{
public:
    BoardStateTests() : juce::UnitTest ("BoardState", "presets") {}

    void runTest() override
    {
        beginTest ("a board captures and re-applies unchanged, mappings and macros included");
        {
            Harness h;
            expect (h.model.insert (0, "test.gain"));
            expect (h.model.insert (1, "test.gain"));

            auto* first = h.model.at (0);
            auto* second = h.model.at (1);
            expect (first != nullptr && second != nullptr);

            h.registry.set (first->paramIndices()[0], 1.25f);
            h.registry.set (first->onParamIndex(), 0.0f);            // this pedal is switched off

            // A macro over the second pedal, and a mapping onto the first pedal's switch.
            h.macros.setName (2, "WRECK");
            h.macros.setTargets (2, { { second->paramIndices()[0], 0.0f, 4.0f, pb::MacroBank::Curve::Linear } });
            h.registry.set (h.macros.macroParamIndex (2), 0.25f);
            h.macros.update();                                        // the second pedal's gain becomes 1.0

            h.learn.setMappings ({ Harness::noteMapping (first->onParamIndex()) });

            const auto captured = pb::capture (h.model, h.macros, h.learn, h.registry);

            expectEquals ((int) captured.slots.size(), 2);
            expectEquals (captured.slots[0].typeId, juce::String ("test.gain"));
            expect (! captured.slots[0].on);
            expectEquals ((int) captured.slots[0].params.size(), 1);
            expectEquals (captured.slots[0].params[0].first, juce::String ("gain"));
            expectWithinAbsoluteError (captured.slots[0].params[0].second, 1.25f, 1.0e-6f);
            expectWithinAbsoluteError (captured.slots[1].params[0].second, 1.0f, 1.0e-6f);

            expectEquals ((int) captured.macros.size(), 8);           // all of them, so a load clears the rest
            expectEquals (captured.macros[2].name, juce::String ("WRECK"));
            expectWithinAbsoluteError (captured.macros[2].value, 0.25f, 1.0e-6f);
            expectEquals ((int) captured.macros[2].targets.size(), 1);
            expectEquals (captured.macros[2].targets[0].slot, 1);
            expectEquals (captured.macros[2].targets[0].paramId, juce::String ("gain"));
            expectWithinAbsoluteError (captured.macros[2].targets[0].maxValue, 4.0f, 1.0e-6f);

            expectEquals ((int) captured.mappings.size(), 1);
            expectEquals (captured.mappings[0].source, juce::String ("note"));
            expectEquals (captured.mappings[0].number, 36);
            expectEquals (captured.mappings[0].action, juce::String ("toggle"));
            expectEquals (captured.mappings[0].target, juce::String ("slot/0/on"));

            // Wipe everything, then rebuild the board from the data.
            h.model.clear();
            h.learn.clearAll();
            h.macros.setName (2, "GONE");
            h.macros.setTargets (2, {});

            const auto applied = pb::apply (captured, h.model, h.macros, h.learn, h.registry);

            expect (applied.ok, applied.error);
            expectEquals (applied.warnings.size(), 0);
            expectEquals (h.model.size(), 2);
            expectWithinAbsoluteError (h.registry.get (h.model.at (0)->paramIndices()[0]), 1.25f, 1.0e-6f);
            expect (h.registry.get (h.model.at (0)->onParamIndex()) < 0.5f);
            expectWithinAbsoluteError (h.registry.get (h.model.at (1)->paramIndices()[0]), 1.0f, 1.0e-5f);
            expectEquals (h.macros.getName (2), juce::String ("WRECK"));
            expectWithinAbsoluteError (h.registry.get (h.macros.macroParamIndex (2)), 0.25f, 1.0e-6f);
            expectEquals ((int) h.macros.getTargets (2).size(), 1);
            expectEquals ((int) h.learn.mappings().size(), 1);

            // Capturing again gives the same thing: a save/load cycle is stable.
            const auto again = pb::capture (h.model, h.macros, h.learn, h.registry);
            expectEquals (pb::toJson (again), pb::toJson (captured));
        }

        beginTest ("a captured board goes through the file format and rebuilds the same board");
        {
            Harness h;
            expect (h.model.insert (0, "test.gain"));
            h.registry.set (h.model.at (0)->paramIndices()[0], 0.75f);
            h.learn.setMappings ({ Harness::noteMapping (h.model.at (0)->onParamIndex(), 44) });

            const auto captured = pb::capture (h.model, h.macros, h.learn, h.registry);
            const auto json = pb::toJson (captured);

            pb::BoardData reread;
            const auto loaded = pb::fromJson (json, reread);
            expect (loaded.ok, loaded.error);
            expectEquals (loaded.warnings.size(), 0);

            h.model.clear();
            h.learn.clearAll();

            const auto applied = pb::apply (reread, h.model, h.macros, h.learn, h.registry);
            expect (applied.ok, applied.error);
            expectEquals (applied.warnings.size(), 0);
            expectEquals (h.model.size(), 1);
            expectWithinAbsoluteError (h.registry.get (h.model.at (0)->paramIndices()[0]), 0.75f, 1.0e-6f);
            expectEquals ((int) h.learn.mappings().size(), 1);
            expectEquals (h.learn.mappings()[0].source.number, 44);

            expectEquals (pb::toJson (pb::capture (h.model, h.macros, h.learn, h.registry)), json);
        }

        beginTest ("an unknown pedal leaves an empty position and the mapping after it still finds its pedal");
        {
            Harness h;

            pb::BoardData data;
            data.slots.push_back (Harness::slot ("test.gain", "gain", 0.5f));
            data.slots.push_back (Harness::slot (""));                          // an empty position
            data.slots.push_back (Harness::slot ("pump.notreal"));              // this build has no such pedal
            data.slots.push_back (Harness::slot ("test.gain3", "gain", 2.5f));

            pb::BoardData::MappingData mapping;
            mapping.source = "note";
            mapping.number = 40;
            mapping.action = "setValue";
            mapping.target = "slot/3/gain";
            mapping.minValue = 0.0f;
            mapping.maxValue = 4.0f;
            data.mappings.push_back (mapping);

            const auto result = pb::apply (data, h.model, h.macros, h.learn, h.registry);

            expect (result.ok, result.error);
            expectEquals (h.model.size(), 2);                     // the empty and the unknown position hold nothing
            expect (result.warnings.size() >= 1);
            expectEquals (h.model.at (0)->getEffect()->typeId(), juce::String ("test.gain"));
            expectEquals (h.model.at (1)->getEffect()->typeId(), juce::String ("test.gain3"));
            expectWithinAbsoluteError (h.registry.get (h.model.at (0)->paramIndices()[0]), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (h.registry.get (h.model.at (1)->paramIndices()[0]), 2.5f, 1.0e-6f);

            // "slot/3/gain" was the fourth position in the file; its pedal is now model slot 1.
            expectEquals ((int) h.learn.mappings().size(), 1);
            expectEquals (h.learn.mappings()[0].paramIndex, h.model.at (1)->paramIndices()[0]);

            // Saved again, the target names the position the pedal actually has.
            const auto recaptured = pb::capture (h.model, h.macros, h.learn, h.registry);
            expectEquals ((int) recaptured.slots.size(), 2);
            expectEquals (recaptured.mappings[0].target, juce::String ("slot/1/gain"));
        }

        beginTest ("a mapping or macro target on a pedal the board does not have is dropped, with a warning");
        {
            Harness h;

            pb::BoardData data;
            data.slots.push_back (Harness::slot ("test.gain"));

            pb::BoardData::MappingData unknownAction;
            unknownAction.source = "cc";
            unknownAction.number = 7;
            unknownAction.action = "wiggle";
            data.mappings.push_back (unknownAction);

            pb::BoardData::MappingData missingPedal;
            missingPedal.source = "cc";
            missingPedal.number = 11;
            missingPedal.action = "setValue";
            missingPedal.target = "slot/9/gain";
            data.mappings.push_back (missingPedal);

            pb::BoardData::MacroData macro;
            macro.name = "M";
            macro.targets.push_back ({ 9, "gain", 0.0f, 1.0f, "linear" });
            macro.targets.push_back ({ 0, "nosuch", 0.0f, 1.0f, "linear" });
            data.macros.push_back (macro);

            const auto result = pb::apply (data, h.model, h.macros, h.learn, h.registry);

            expect (result.ok, result.error);
            expect (h.learn.mappings().empty());
            expect (h.macros.getTargets (0).empty());
            expect (result.warnings.size() >= 3, result.warnings.joinIntoString (" | "));
        }

        beginTest ("a preset mapping is written without a target and comes back ready to fire");
        {
            Harness h;

            pb::Mapping preset;
            preset.source = { pb::SourceType::ProgramChange, 0, 4 };
            preset.action = pb::ActionType::PresetSelect;
            h.learn.setMappings ({ preset });

            const auto captured = pb::capture (h.model, h.macros, h.learn, h.registry);
            expectEquals ((int) captured.mappings.size(), 1);
            expectEquals (captured.mappings[0].action, juce::String ("presetSelect"));
            expectEquals (captured.mappings[0].source, juce::String ("programChange"));
            expect (captured.mappings[0].target.isEmpty());

            h.learn.clearAll();
            const auto applied = pb::apply (captured, h.model, h.macros, h.learn, h.registry);

            expect (applied.ok, applied.error);
            expectEquals (applied.warnings.size(), 0);
            expectEquals ((int) h.learn.mappings().size(), 1);
            expect (h.learn.mappings()[0].action == pb::ActionType::PresetSelect);
            expectEquals (h.learn.mappings()[0].paramIndex, pb::ParameterRegistry::kInvalid);
            expectEquals (h.learn.mappings()[0].source.number, 4);
        }

        beginTest ("loading an empty board clears slots, macros and mappings rather than inheriting them");
        {
            Harness h;
            expect (h.model.insert (0, "test.gain"));
            h.learn.setMappings ({ Harness::noteMapping (h.model.at (0)->onParamIndex()) });
            h.macros.setName (0, "KEPT?");
            h.macros.setTargets (0, { { h.model.at (0)->paramIndices()[0], 0.0f, 1.0f,
                                        pb::MacroBank::Curve::Linear } });

            const pb::BoardData empty;
            const auto result = pb::apply (empty, h.model, h.macros, h.learn, h.registry);

            expect (result.ok, result.error);
            expectEquals (h.model.size(), 0);
            expect (h.learn.mappings().empty());
            expect (h.macros.getTargets (0).empty());
            expectEquals (h.macros.getName (0), juce::String ("MACRO 1"));    // back to its default name
        }

        beginTest ("the board trims travel with the file when the registry has them");
        {
            Harness h;

            int inputTrim = h.registry.find ("board/inputTrim");

            if (inputTrim == pb::ParameterRegistry::kInvalid)
            {
                pb::ParameterRegistry::Entry entry;
                entry.id = "board/inputTrim";
                entry.displayName = "Input";
                entry.minValue = -24.0f;
                entry.maxValue = 24.0f;
                inputTrim = h.registry.add (entry);
            }

            h.registry.set (inputTrim, -3.0f);

            const auto captured = pb::capture (h.model, h.macros, h.learn, h.registry);
            expectWithinAbsoluteError (captured.inputTrimDb, -3.0f, 1.0e-6f);

            h.registry.set (inputTrim, 0.0f);
            expect (pb::apply (captured, h.model, h.macros, h.learn, h.registry).ok);
            expectWithinAbsoluteError (h.registry.get (inputTrim), -3.0f, 1.0e-6f);
        }
    }
};

static BoardStateTests boardStateTests;
