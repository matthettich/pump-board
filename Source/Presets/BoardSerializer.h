#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include <juce_core/juce_core.h>
#include <vector>

namespace pb
{
// Plain data mirror of a board, independent of live objects. Matches schemas/board.schema.json.
struct BoardData
{
    static constexpr int kSchemaVersion = 1;

    struct SlotData
    {
        juce::String typeId;                 // "" = empty slot
        bool on = true;
        std::vector<std::pair<juce::String, float>> params; // paramId -> value
        juce::String pedalFile;              // set when typeId == "pedal.custom"
    };

    struct MacroTargetData { int slot = 0; juce::String paramId; float minValue = 0, maxValue = 1; juce::String curve = "linear"; };
    struct MacroData { juce::String name; float value = 0; std::vector<MacroTargetData> targets; };
    struct MappingData { juce::String source; int channel = 0; int number = 0; juce::String action; juce::String target; float minValue = 0, maxValue = 1; };

    juce::String name;
    float inputTrimDb = 0, outputTrimDb = 0;
    std::vector<SlotData> slots;             // max 16
    std::vector<MacroData> macros;           // max 8
    std::vector<MappingData> mappings;
};

struct LoadResult
{
    bool ok = false;
    juce::StringArray warnings;              // e.g. "Slot 4: unknown effect 'pump.foo', left empty"
    juce::String error;                      // set only when ok == false
};

// Message thread only. Never throws, never crashes on bad input.
juce::String toJson (const BoardData& board);
LoadResult fromJson (const juce::String& json, BoardData& out);
} // namespace pb
