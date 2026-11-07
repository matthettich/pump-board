#include <juce_core/juce_core.h>
#include "Presets/BoardSerializer.h"

namespace
{
pb::BoardData makeFullBoard()
{
    pb::BoardData data;
    data.name = "Clean to chaos";
    data.inputTrimDb = -1.5f;
    data.outputTrimDb = 2.0f;

    pb::BoardData::SlotData tube;
    tube.typeId = "pump.tube";
    tube.on = true;
    tube.params.emplace_back ("drive", 0.4f);
    tube.params.emplace_back ("mix", 1.0f);

    pb::BoardData::SlotData custom;
    custom.typeId = "pedal.custom";
    custom.on = false;
    custom.pedalFile = "Pedals/fuzz.pedal";
    custom.params.emplace_back ("fuzz", 0.75f);

    data.slots.push_back (tube);
    data.slots.push_back (custom);

    pb::BoardData::MacroData macro;
    macro.name = "Wreck";
    macro.value = 0.25f;
    macro.targets.push_back ({ 0, "drive", 0.2f, 1.0f, "linear" });
    macro.targets.push_back ({ 1, "fuzz", 0.0f, 10.0f, "exponential" });
    data.macros.push_back (macro);

    pb::BoardData::MappingData toggle;
    toggle.source = "note";
    toggle.channel = 0;
    toggle.number = 36;
    toggle.action = "toggle";
    toggle.target = "slot/1/on";
    data.mappings.push_back (toggle);

    pb::BoardData::MappingData next;
    next.source = "key";
    next.channel = 0;
    next.number = 'n';
    next.action = "presetNext";
    next.target = "board/bypass";
    next.minValue = 0.1f;
    next.maxValue = 0.9f;
    data.mappings.push_back (next);

    return data;
}

juce::String slotsJson (int count)
{
    juce::String json = "{\"schemaVersion\":1,\"slots\":[";

    for (int i = 0; i < count; ++i)
        json << (i > 0 ? "," : "") << "{\"typeId\":\"pump.tube\"}";

    json << "]}";
    return json;
}
} // namespace

class BoardSerializerTests final : public juce::UnitTest
{
public:
    BoardSerializerTests() : juce::UnitTest ("BoardSerializer", "presets") {}

    void runTest() override
    {
        beginTest ("a board survives data -> json -> data -> json unchanged");
        {
            const auto original = makeFullBoard();
            const auto json1 = pb::toJson (original);

            expect (json1.isNotEmpty());
            expect (json1.containsIgnoreCase ("\"schemaVersion\""));

            pb::BoardData back;
            const auto result = pb::fromJson (json1, back);

            expect (result.ok, result.error);
            expectEquals (result.warnings.size(), 0);
            expectEquals (result.error, juce::String());

            expectEquals (back.name, juce::String ("Clean to chaos"));
            expectWithinAbsoluteError (back.inputTrimDb, -1.5f, 1.0e-6f);
            expectWithinAbsoluteError (back.outputTrimDb, 2.0f, 1.0e-6f);

            expectEquals ((int) back.slots.size(), 2);
            expectEquals (back.slots[0].typeId, juce::String ("pump.tube"));
            expect (back.slots[0].on);
            expectEquals ((int) back.slots[0].params.size(), 2);
            expectEquals (back.slots[0].params[0].first, juce::String ("drive"));
            expectWithinAbsoluteError (back.slots[0].params[0].second, 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (back.slots[0].params[1].second, 1.0f, 1.0e-6f);

            expectEquals (back.slots[1].typeId, juce::String ("pedal.custom"));
            expect (! back.slots[1].on);
            expectEquals (back.slots[1].pedalFile, juce::String ("Pedals/fuzz.pedal"));

            expectEquals ((int) back.macros.size(), 1);
            expectEquals (back.macros[0].name, juce::String ("Wreck"));
            expectWithinAbsoluteError (back.macros[0].value, 0.25f, 1.0e-6f);
            expectEquals ((int) back.macros[0].targets.size(), 2);
            expectEquals (back.macros[0].targets[0].slot, 0);
            expectEquals (back.macros[0].targets[0].paramId, juce::String ("drive"));
            expectWithinAbsoluteError (back.macros[0].targets[0].minValue, 0.2f, 1.0e-6f);
            expectEquals (back.macros[0].targets[1].curve, juce::String ("exponential"));

            expectEquals ((int) back.mappings.size(), 2);
            expectEquals (back.mappings[0].source, juce::String ("note"));
            expectEquals (back.mappings[0].number, 36);
            expectEquals (back.mappings[0].action, juce::String ("toggle"));
            expectEquals (back.mappings[0].target, juce::String ("slot/1/on"));
            expectEquals (back.mappings[1].action, juce::String ("presetNext"));
            expectEquals (back.mappings[1].number, (int) 'n');
            expectWithinAbsoluteError (back.mappings[1].maxValue, 0.9f, 1.0e-6f);

            expectEquals (pb::toJson (back), json1);
        }

        beginTest ("every optional field missing still gives a usable board");
        {
            pb::BoardData back;
            const auto result = pb::fromJson ("{\"schemaVersion\":1}", back);

            expect (result.ok, result.error);
            expectEquals (result.warnings.size(), 0);
            expectEquals (back.name, juce::String (""));
            expectWithinAbsoluteError (back.inputTrimDb, 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (back.outputTrimDb, 0.0f, 1.0e-6f);
            expect (back.slots.empty());
            expect (back.macros.empty());
            expect (back.mappings.empty());

            pb::BoardData slotOnly;
            const auto slotResult = pb::fromJson ("{\"schemaVersion\":1,\"slots\":[{\"typeId\":\"pump.echo\"}]}",
                                                  slotOnly);

            expect (slotResult.ok, slotResult.error);
            expectEquals (slotResult.warnings.size(), 0);
            expectEquals ((int) slotOnly.slots.size(), 1);
            expectEquals (slotOnly.slots[0].typeId, juce::String ("pump.echo"));
            expect (slotOnly.slots[0].on);                       // "on" defaults to true, not false
            expect (slotOnly.slots[0].params.empty());
            expect (slotOnly.slots[0].pedalFile.isEmpty());
            expectEquals (slotOnly.name, juce::String (""));

            pb::BoardData emptySlot;
            const auto emptyResult = pb::fromJson ("{\"schemaVersion\":1,\"slots\":[{}]}", emptySlot);
            expect (emptyResult.ok, emptyResult.error);
            expectEquals (emptySlot.slots[0].typeId, juce::String (""));
            expect (emptySlot.slots[0].params.empty());
        }

        beginTest ("garbage, a newer file and a missing version all fail cleanly");
        {
            const char* const bad[] = { "", "   ", "{", "not json", "[]", "\"a board\"", "42",
                                        "{\"schemaVersion\":1", "{\"slots\":[]}", "{\"schemaVersion\":\"one\"}",
                                        "{\"schemaVersion\":0}" };

            for (const auto* text : bad)
            {
                pb::BoardData out;
                out.name = "keep me";

                const auto result = pb::fromJson (text, out);

                expect (! result.ok, juce::String ("should have failed: ") + text);
                expect (result.error.isNotEmpty(), juce::String ("no error for: ") + text);
                expectEquals (out.name, juce::String ("keep me"));   // a failed load touches nothing
            }

            pb::BoardData newer;
            const auto result = pb::fromJson ("{\"schemaVersion\":2,\"slots\":[]}", newer);
            expect (! result.ok);
            expect (result.error.containsIgnoreCase ("newer"));
        }

        beginTest ("more slots or macros than fit are cut with a warning");
        {
            pb::BoardData out;
            const auto many = pb::fromJson (slotsJson (20), out);

            expect (many.ok, many.error);
            expectEquals ((int) out.slots.size(), 16);
            expect (many.warnings.size() >= 1);

            juce::String json = "{\"schemaVersion\":1,\"macros\":[";

            for (int i = 0; i < 12; ++i)
                json << (i > 0 ? "," : "") << "{\"name\":\"M" << i << "\"}";

            json << "]}";

            pb::BoardData macros;
            const auto macroResult = pb::fromJson (json, macros);

            expect (macroResult.ok, macroResult.error);
            expectEquals ((int) macros.macros.size(), 8);
            expect (macroResult.warnings.size() >= 1);
            expectEquals (macros.macros[0].name, juce::String ("M0"));
        }

        beginTest ("unreadable pieces are dropped or repaired, each with a warning");
        {
            const juce::String json = R"({
                "schemaVersion": 1,
                "slots": [
                    "not an object",
                    { "typeId": 7, "params": { "drive": 0.5, "mix": "loud" } }
                ],
                "macros": [
                    { "name": "Wreck", "value": 4.0, "targets": [
                        { "slot": 0, "paramId": "drive", "curve": "wobbly" },
                        { "slot": 99, "paramId": "mix" },
                        { "slot": 1 },
                        "not an object"
                    ] }
                ],
                "mappings": [
                    { "source": "note", "number": 36, "action": "toggle", "target": "slot/0/on" },
                    { "source": "carrier-pigeon", "number": 1, "action": "toggle" },
                    { "source": "cc", "number": 7, "action": "wiggle" }
                ]
            })";

            pb::BoardData out;
            const auto result = pb::fromJson (json, out);

            expect (result.ok, result.error);

            // The string slot is kept as an empty slot, and the bad one is repaired.
            expectEquals ((int) out.slots.size(), 2);
            expect (out.slots[0].typeId.isEmpty());
            expect (out.slots[1].typeId.isEmpty());
            expectEquals ((int) out.slots[1].params.size(), 1);
            expectWithinAbsoluteError (out.slots[1].params[0].second, 0.5f, 1.0e-6f);

            // A macro value over 1 is pulled back; one target survives, repaired to linear.
            expectWithinAbsoluteError (out.macros[0].value, 1.0f, 1.0e-6f);
            expectEquals ((int) out.macros[0].targets.size(), 1);
            expectEquals (out.macros[0].targets[0].paramId, juce::String ("drive"));
            expectEquals (out.macros[0].targets[0].curve, juce::String ("linear"));

            // Only the readable mapping is kept.
            expectEquals ((int) out.mappings.size(), 1);
            expectEquals (out.mappings[0].target, juce::String ("slot/0/on"));

            expect (result.warnings.size() >= 4,
                    "warnings: " + result.warnings.joinIntoString (" | "));

            for (const auto& warning : result.warnings)
                expect (warning.isNotEmpty());
        }

        beginTest ("a written file is JSON a person can read");
        {
            const auto json = pb::toJson (makeFullBoard());

            expect (json.contains ("\"typeId\": \"pump.tube\""), json);
            expect (json.contains ("\"curve\": \"exponential\""), json);
            expect (json.contains ("\"drive\""), json);
            expect (! json.contains ("0.4000000059"), json);            // six places, not the float's full tail
            expect (! json.contains ("e-"), json);                     // no exponent soup for these values
        }
    }
};

static BoardSerializerTests boardSerializerTests;
