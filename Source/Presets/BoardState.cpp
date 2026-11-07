#include "Presets/BoardState.h"
#include "Engine/ChainSnapshot.h"

namespace pb
{
namespace
{
juce::String actionToString (ActionType action)
{
    switch (action)
    {
        case ActionType::Toggle:       return "toggle";
        case ActionType::Momentary:    return "momentary";
        case ActionType::CCThreshold:  return "ccThreshold";
        case ActionType::SetValue:     return "setValue";
        case ActionType::PresetSelect: return "presetSelect";
        case ActionType::PresetNext:   return "presetNext";
        case ActionType::PresetPrev:   return "presetPrev";
        default: break;
    }

    return "toggle";
}

bool actionFromString (const juce::String& text, ActionType& out) noexcept
{
    if (text == "toggle")       { out = ActionType::Toggle;       return true; }
    if (text == "momentary")    { out = ActionType::Momentary;    return true; }
    if (text == "ccThreshold")  { out = ActionType::CCThreshold;  return true; }
    if (text == "setValue")     { out = ActionType::SetValue;     return true; }
    if (text == "presetSelect") { out = ActionType::PresetSelect; return true; }
    if (text == "presetNext")   { out = ActionType::PresetNext;   return true; }
    if (text == "presetPrev")   { out = ActionType::PresetPrev;   return true; }

    return false;
}

bool isPresetAction (ActionType action) noexcept
{
    return action == ActionType::PresetSelect
        || action == ActionType::PresetNext
        || action == ActionType::PresetPrev;
}

juce::String sourceToString (SourceType type)
{
    switch (type)
    {
        case SourceType::Note:          return "note";
        case SourceType::CC:            return "cc";
        case SourceType::ProgramChange: return "programChange";
        case SourceType::Key:           return "key";
        default: break;
    }

    return "note";
}

bool sourceFromString (const juce::String& text, SourceType& out) noexcept
{
    if (text == "note")          { out = SourceType::Note;          return true; }
    if (text == "cc")            { out = SourceType::CC;            return true; }
    if (text == "programChange") { out = SourceType::ProgramChange; return true; }
    if (text == "key")           { out = SourceType::Key;           return true; }

    return false;
}

juce::String curveToString (MacroBank::Curve curve)
{
    switch (curve)
    {
        case MacroBank::Curve::Linear:      return "linear";
        case MacroBank::Curve::Exponential: return "exponential";
        case MacroBank::Curve::Logarithmic: return "logarithmic";
        case MacroBank::Curve::Inverted:    return "inverted";
        default: break;
    }

    return "linear";
}

MacroBank::Curve curveFromString (const juce::String& text) noexcept
{
    if (text == "exponential") return MacroBank::Curve::Exponential;
    if (text == "logarithmic") return MacroBank::Curve::Logarithmic;
    if (text == "inverted")    return MacroBank::Curve::Inverted;

    return MacroBank::Curve::Linear;
}

// "slot/<uid>/<paramId>" -> its two halves. Slot uids and param ids never contain '/', so the split is safe.
bool splitSlotParam (const juce::String& id, juce::String& uid, juce::String& paramId)
{
    if (! id.startsWith ("slot/"))
        return false;

    const auto rest = id.substring (5);
    const int slash = rest.indexOfChar ('/');

    if (slash <= 0 || slash >= rest.length() - 1)
        return false;

    uid = rest.substring (0, slash);
    paramId = rest.substring (slash + 1);
    return true;
}

// A registry index -> the file's own way of naming a target, or empty when this board cannot express it.
juce::String targetStringFor (int paramIndex, const BoardModel& model, const ParameterRegistry& registry)
{
    if (paramIndex == ParameterRegistry::kInvalid)
        return {};

    const auto id = registry.info (paramIndex).id;
    juce::String uid, paramId;

    if (! splitSlotParam (id, uid, paramId))
        return id;                                   // "board/bypass", "macro/3": already position-free

    const int position = model.indexOfUid (uid);
    return position >= 0 ? "slot/" + juce::String (position) + "/" + paramId : juce::String();
}

// A target string -> a registry index, using the slots the load just created. Empty positions (an empty
// slot, or a pedal this build does not have) resolve to nothing, and the caller warns.
int paramIndexForTarget (const juce::String& target, const std::vector<juce::String>& slotUids,
                         const ParameterRegistry& registry)
{
    juce::String uid, paramId;

    if (! splitSlotParam (target, uid, paramId))
        return registry.find (target);               // "board/bypass", "macro/3"

    const int position = uid.getIntValue();

    if (position < 0 || position >= (int) slotUids.size())
        return ParameterRegistry::kInvalid;

    const auto& realUid = slotUids[(size_t) position];

    if (realUid.isEmpty())
        return ParameterRegistry::kInvalid;

    return registry.find ("slot/" + realUid + "/" + paramId);
}

int macroTargetIndex (const BoardData::MacroTargetData& target, const std::vector<juce::String>& slotUids,
                      const ParameterRegistry& registry)
{
    if (target.slot < 0 || target.slot >= (int) slotUids.size() || target.paramId.isEmpty())
        return ParameterRegistry::kInvalid;

    const auto& uid = slotUids[(size_t) target.slot];

    if (uid.isEmpty())
        return ParameterRegistry::kInvalid;

    return registry.find ("slot/" + uid + "/" + target.paramId);
}

juce::String slotName (int position)
{
    return "Slot " + juce::String (position);
}
} // namespace

BoardData capture (const BoardModel& model, const MacroBank& macros, const LearnController& learn,
                   const ParameterRegistry& registry)
{
    BoardData data;

    // The preset manager fills the name in when it saves; a captured board does not know it yet.

    for (int i = 0; i < model.size(); ++i)
    {
        BoardData::SlotData slotData;
        auto* slot = model.at (i);
        auto* effect = slot != nullptr ? slot->getEffect() : nullptr;

        if (effect == nullptr)
        {
            data.slots.push_back (std::move (slotData));      // an empty position: the slot keeps its place
            continue;
        }

        slotData.typeId = effect->typeId();

        const int onIndex = slot->onParamIndex();
        slotData.on = onIndex == ParameterRegistry::kInvalid || registry.get (onIndex) >= 0.5f;

        const auto params = effect->params();
        const auto& indices = slot->paramIndices();

        for (size_t p = 0; p < params.size() && p < indices.size(); ++p)
            if (indices[p] != ParameterRegistry::kInvalid)
                slotData.params.emplace_back (params[p].id, registry.get (indices[p]));

        data.slots.push_back (std::move (slotData));
    }

    const int inputTrim = registry.find ("board/inputTrim");
    const int outputTrim = registry.find ("board/outputTrim");

    if (inputTrim != ParameterRegistry::kInvalid)
        data.inputTrimDb = registry.get (inputTrim);

    if (outputTrim != ParameterRegistry::kInvalid)
        data.outputTrimDb = registry.get (outputTrim);

    // Every macro is written, empty or not, so loading a board also clears the previous board's macros.
    for (int i = 0; i < MacroBank::kNumMacros; ++i)
    {
        BoardData::MacroData macroData;
        macroData.name = macros.getName (i);

        const int macroIndex = macros.macroParamIndex (i);
        macroData.value = macroIndex != ParameterRegistry::kInvalid
                            ? juce::jlimit (0.0f, 1.0f, registry.get (macroIndex))
                            : 0.0f;

        for (const auto& target : macros.getTargets (i))
        {
            if (target.paramIndex == ParameterRegistry::kInvalid)
                continue;

            const auto id = registry.info (target.paramIndex).id;
            juce::String uid, paramId;

            // Only a pedal's own parameter can be a macro target: "slot/<uid>/<paramId>".
            if (! splitSlotParam (id, uid, paramId))
                continue;

            const int position = model.indexOfUid (uid);

            if (position < 0)
                continue;

            BoardData::MacroTargetData targetData;
            targetData.slot = position;
            targetData.paramId = paramId;
            targetData.minValue = target.minValue;
            targetData.maxValue = target.maxValue;
            targetData.curve = curveToString (target.curve);
            macroData.targets.push_back (std::move (targetData));
        }

        data.macros.push_back (std::move (macroData));
    }

    for (const auto& mapping : learn.mappings())
    {
        BoardData::MappingData mappingData;
        mappingData.source = sourceToString (mapping.source.type);
        mappingData.channel = (int) mapping.source.channel;
        mappingData.number = mapping.source.number;
        mappingData.action = actionToString (mapping.action);
        mappingData.minValue = mapping.minValue;
        mappingData.maxValue = mapping.maxValue;

        if (! isPresetAction (mapping.action))
        {
            mappingData.target = targetStringFor (mapping.paramIndex, model, registry);

            // A mapping onto a pedal that is no longer there cannot be written down.
            if (mappingData.target.isEmpty())
                continue;
        }

        data.mappings.push_back (std::move (mappingData));
    }

    return data;
}

LoadResult apply (const BoardData& data, BoardModel& model, MacroBank& macros, LearnController& learn,
                  ParameterRegistry& registry)
{
    LoadResult result;

    model.clear();

    const int slotCount = juce::jmin ((int) data.slots.size(), ChainSnapshot::kMaxSlots);

    if ((int) data.slots.size() > ChainSnapshot::kMaxSlots)
        result.warnings.add ("The board has " + juce::String ((int) data.slots.size()) + " slots; only the first "
                             + juce::String (ChainSnapshot::kMaxSlots) + " were loaded");

    // One entry per file position, holding the uid of the slot created for it (empty when there is none).
    // That is what lets a target written as "slot/2/mix" find its pedal after a slot was dropped.
    std::vector<juce::String> slotUids;
    slotUids.reserve ((size_t) slotCount);

    for (int i = 0; i < slotCount; ++i)
    {
        const auto& slotData = data.slots[(size_t) i];

        if (slotData.typeId.isEmpty())
        {
            slotUids.push_back ({});
            continue;
        }

        if (! model.insert (model.size(), slotData.typeId))
        {
            result.warnings.add (slotName (i) + ": unknown effect '" + slotData.typeId + "', left empty");
            slotUids.push_back ({});
            continue;
        }

        auto* slot = model.at (model.size() - 1);

        if (slot == nullptr)
        {
            slotUids.push_back ({});
            continue;
        }

        slotUids.push_back (slot->getUid());

        auto* effect = slot->getEffect();

        if (effect == nullptr)
            continue;

        const auto params = effect->params();
        const auto& indices = slot->paramIndices();

        for (const auto& [paramId, value] : slotData.params)
        {
            bool found = false;

            for (size_t p = 0; p < params.size() && p < indices.size(); ++p)
            {
                if (params[p].id != paramId)
                    continue;

                if (indices[p] != ParameterRegistry::kInvalid)
                    registry.set (indices[p], value);

                found = true;
                break;
            }

            if (! found)
                result.warnings.add (slotName (i) + ": unknown parameter '" + paramId + "', ignored");
        }

        if (slot->onParamIndex() != ParameterRegistry::kInvalid)
            registry.set (slot->onParamIndex(), slotData.on ? 1.0f : 0.0f);
    }

    const int inputTrim = registry.find ("board/inputTrim");
    const int outputTrim = registry.find ("board/outputTrim");

    if (inputTrim != ParameterRegistry::kInvalid)
        registry.set (inputTrim, data.inputTrimDb);

    if (outputTrim != ParameterRegistry::kInvalid)
        registry.set (outputTrim, data.outputTrimDb);

    for (int i = 0; i < MacroBank::kNumMacros; ++i)
    {
        std::vector<MacroBank::Target> targets;
        juce::String name;
        float value = 0.0f;

        if (i < (int) data.macros.size())
        {
            const auto& macroData = data.macros[(size_t) i];
            name = macroData.name;
            value = juce::jlimit (0.0f, 1.0f, macroData.value);

            for (const auto& targetData : macroData.targets)
            {
                const int index = macroTargetIndex (targetData, slotUids, registry);

                if (index == ParameterRegistry::kInvalid)
                {
                    result.warnings.add ("Macro " + juce::String (i + 1) + ": target slot "
                                         + juce::String (targetData.slot) + " '" + targetData.paramId
                                         + "' is not on this board, skipped");
                    continue;
                }

                MacroBank::Target target;
                target.paramIndex = index;
                target.minValue = targetData.minValue;
                target.maxValue = targetData.maxValue;
                target.curve = curveFromString (targetData.curve);

                targets.push_back (target);
            }
        }

        macros.setName (i, name);
        macros.setTargets (i, std::move (targets));

        const int macroIndex = macros.macroParamIndex (i);

        if (macroIndex != ParameterRegistry::kInvalid)
            registry.set (macroIndex, value);
    }

    // The targets above were written at the previous board's knob positions; this writes them where the
    // loaded board left the knobs.
    macros.update();

    std::vector<Mapping> mappings;
    mappings.reserve (data.mappings.size());

    for (size_t i = 0; i < data.mappings.size(); ++i)
    {
        const auto& mappingData = data.mappings[i];
        const auto where = "Mapping " + juce::String ((int) i + 1) + ": ";

        Mapping mapping;

        if (! sourceFromString (mappingData.source, mapping.source.type))
        {
            result.warnings.add (where + "unknown source '" + mappingData.source + "', skipped");
            continue;
        }

        if (! actionFromString (mappingData.action, mapping.action))
        {
            result.warnings.add (where + "unknown action '" + mappingData.action + "', skipped");
            continue;
        }

        mapping.source.channel = (uint8_t) juce::jlimit (0, 16, mappingData.channel);
        mapping.source.number = mappingData.number;
        mapping.minValue = mappingData.minValue;
        mapping.maxValue = mappingData.maxValue;

        if (! isPresetAction (mapping.action))
        {
            mapping.paramIndex = paramIndexForTarget (mappingData.target, slotUids, registry);

            if (mapping.paramIndex == ParameterRegistry::kInvalid)
            {
                result.warnings.add (where + "target '" + mappingData.target
                                     + "' is not on this board, skipped");
                continue;
            }
        }

        mappings.push_back (mapping);
    }

    learn.setMappings (std::move (mappings));

    result.ok = true;
    return result;
}
} // namespace pb
