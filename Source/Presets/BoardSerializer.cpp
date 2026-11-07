#include "Presets/BoardSerializer.h"
#include <cmath>

namespace pb
{
namespace
{
constexpr int kMaxSlots = 16;
constexpr int kMaxMacros = 8;
constexpr int kMaxChannel = 16;

bool isNumber (const juce::var& v) noexcept
{
    return v.isInt() || v.isDouble();
}

juce::String stringOr (const juce::var& v, const juce::String& fallback = {})
{
    return v.isString() ? v.toString() : fallback;
}

bool boolOr (const juce::var& v, bool fallback) noexcept
{
    return v.isBool() ? (bool) v : fallback;
}

double doubleOr (const juce::var& v, double fallback) noexcept
{
    if (! isNumber (v))
        return fallback;

    const double d = (double) v;
    return std::isfinite (d) ? d : fallback;
}

int intOr (const juce::var& v, int fallback) noexcept
{
    if (! isNumber (v))
        return fallback;

    const double d = (double) v;

    if (! std::isfinite (d))
        return fallback;

    return (int) juce::jlimit (-1.0e9, 1.0e9, std::round (d));
}

// These return the schema's own spelling, or an empty string when the value is not one of ours. The
// comparison is case-insensitive (a hand-written file may say "presetnext") but what comes back is always
// the camelCase form the schema lists, so a load followed by a save does not rewrite the file.
juce::String canonicalSource (const juce::String& raw)
{
    const auto s = raw.trim().toLowerCase();

    if (s == "note")          return "note";
    if (s == "cc")            return "cc";
    if (s == "programchange") return "programChange";
    if (s == "key")           return "key";

    return {};
}

juce::String canonicalAction (const juce::String& raw)
{
    const auto s = raw.trim().toLowerCase();

    if (s == "toggle")      return "toggle";
    if (s == "momentary")   return "momentary";
    if (s == "ccthreshold") return "ccThreshold";
    if (s == "setvalue")    return "setValue";
    if (s == "presetselect") return "presetSelect";
    if (s == "presetnext")  return "presetNext";
    if (s == "presetprev")  return "presetPrev";

    return {};
}

juce::String canonicalCurve (const juce::String& raw)
{
    const auto s = raw.trim().toLowerCase();

    if (s == "linear")      return "linear";
    if (s == "exponential") return "exponential";
    if (s == "logarithmic") return "logarithmic";
    if (s == "inverted")    return "inverted";

    return {};
}

juce::String slotWhere (int position)
{
    return "Slot " + juce::String (position) + ": ";
}

// ---- reading ----

BoardData::SlotData readSlot (const juce::var& item, int position, juce::StringArray& warnings)
{
    BoardData::SlotData slot;
    auto* obj = item.getDynamicObject();

    if (obj == nullptr)
    {
        warnings.add (slotWhere (position) + "not an object, left empty");
        return slot;
    }

    const auto typeVar = obj->getProperty ("typeId");

    if (typeVar.isString())
        slot.typeId = typeVar.toString().trim();
    else if (! typeVar.isVoid())
        warnings.add (slotWhere (position) + "\"typeId\" is not a string, left empty");

    slot.on = boolOr (obj->getProperty ("on"), true);

    const auto paramsVar = obj->getProperty ("params");

    if (paramsVar.isObject())
    {
        for (const auto& named : paramsVar.getDynamicObject()->getProperties())
        {
            if (isNumber (named.value))
                slot.params.emplace_back (named.name.toString(), (float) doubleOr (named.value, 0.0));
            else
                warnings.add (slotWhere (position) + "parameter \"" + named.name.toString()
                              + "\" is not a number, skipped");
        }
    }
    else if (! paramsVar.isVoid())
    {
        warnings.add (slotWhere (position) + "\"params\" is not an object, no values loaded");
    }

    const auto pedalVar = obj->getProperty ("pedalFile");

    if (pedalVar.isString())
        slot.pedalFile = pedalVar.toString();
    else if (! pedalVar.isVoid())
        warnings.add (slotWhere (position) + "\"pedalFile\" is not a string, ignored");

    return slot;
}

BoardData::MacroData readMacro (const juce::var& item, int index, juce::StringArray& warnings)
{
    BoardData::MacroData macro;
    auto* obj = item.getDynamicObject();
    const auto where = "Macro " + juce::String (index + 1) + ": ";

    if (obj == nullptr)
    {
        warnings.add (where + "not an object, left empty");
        return macro;
    }

    if (const auto nameVar = obj->getProperty ("name"); nameVar.isString())
        macro.name = nameVar.toString().trim();

    // A macro lives in 0..1 by construction, so a stray value is pulled back rather than trusted.
    macro.value = juce::jlimit (0.0f, 1.0f, (float) doubleOr (obj->getProperty ("value"), 0.0));

    const auto targetsVar = obj->getProperty ("targets");

    if (targetsVar.isArray())
    {
        for (int t = 0; t < targetsVar.size(); ++t)
        {
            const auto targetWhere = where + "target " + juce::String (t + 1) + ": ";
            auto* targetObj = targetsVar[t].getDynamicObject();

            if (targetObj == nullptr)
            {
                warnings.add (targetWhere + "not an object, skipped");
                continue;
            }

            const auto paramId = stringOr (targetObj->getProperty ("paramId")).trim();
            const int slot = intOr (targetObj->getProperty ("slot"), -1);

            if (paramId.isEmpty())
            {
                warnings.add (targetWhere + "no \"paramId\", skipped");
                continue;
            }

            if (slot < 0 || slot >= kMaxSlots)
            {
                warnings.add (targetWhere + "slot " + juce::String (slot) + " is not 0.."
                              + juce::String (kMaxSlots - 1) + ", skipped");
                continue;
            }

            BoardData::MacroTargetData target;
            target.slot = slot;
            target.paramId = paramId;
            target.minValue = (float) doubleOr (targetObj->getProperty ("min"), 0.0);
            target.maxValue = (float) doubleOr (targetObj->getProperty ("max"), 1.0);

            const auto rawCurve = stringOr (targetObj->getProperty ("curve"), "linear");
            auto curve = canonicalCurve (rawCurve);

            if (curve.isEmpty())
            {
                warnings.add (targetWhere + "unknown curve \"" + rawCurve + "\", using linear");
                curve = "linear";
            }

            target.curve = curve;
            macro.targets.push_back (std::move (target));
        }
    }
    else if (! targetsVar.isVoid())
    {
        warnings.add (where + "\"targets\" is not a list, none loaded");
    }

    return macro;
}

bool readMapping (const juce::var& item, int index, BoardData::MappingData& mapping,
                  juce::StringArray& warnings)
{
    const auto where = "Mapping " + juce::String (index + 1) + ": ";
    auto* obj = item.getDynamicObject();

    if (obj == nullptr)
    {
        warnings.add (where + "not an object, skipped");
        return false;
    }

    // A mapping whose source or action cannot be read is dropped, not guessed at: acting on the wrong
    // footswitch is worse than losing one row, and the warning says so.
    const auto source = canonicalSource (stringOr (obj->getProperty ("source")));

    if (source.isEmpty())
    {
        warnings.add (where + "\"source\" is missing or unknown, skipped");
        return false;
    }

    const auto action = canonicalAction (stringOr (obj->getProperty ("action")));

    if (action.isEmpty())
    {
        warnings.add (where + "\"action\" is missing or unknown, skipped");
        return false;
    }

    mapping.source = source;
    mapping.action = action;
    mapping.channel = juce::jlimit (0, kMaxChannel, intOr (obj->getProperty ("channel"), 0));
    mapping.number = intOr (obj->getProperty ("number"), 0);
    mapping.target = stringOr (obj->getProperty ("target")).trim();
    mapping.minValue = (float) doubleOr (obj->getProperty ("min"), 0.0);
    mapping.maxValue = (float) doubleOr (obj->getProperty ("max"), 1.0);

    return true;
}

// ---- writing ----

template <typename Item, typename WriteFn>
juce::Array<juce::var> asArray (const std::vector<Item>& items, WriteFn&& write)
{
    juce::Array<juce::var> array;

    for (const auto& item : items)
        array.add (write (item));

    return array;
}

juce::var writeSlot (const BoardData::SlotData& slot)
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty ("typeId", slot.typeId);
    obj->setProperty ("on", slot.on);

    auto* params = new juce::DynamicObject();

    for (const auto& [id, value] : slot.params)
        params->setProperty (juce::Identifier (id), (double) value);

    obj->setProperty ("params", juce::var (params));
    obj->setProperty ("pedalFile", slot.pedalFile);

    return juce::var (obj);
}

juce::var writeMacro (const BoardData::MacroData& macro)
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty ("name", macro.name);
    obj->setProperty ("value", (double) macro.value);
    obj->setProperty ("targets", asArray (macro.targets, [] (const BoardData::MacroTargetData& target)
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("slot", target.slot);
        t->setProperty ("paramId", target.paramId);
        t->setProperty ("min", (double) target.minValue);
        t->setProperty ("max", (double) target.maxValue);
        t->setProperty ("curve", target.curve);
        return juce::var (t);
    }));

    return juce::var (obj);
}

juce::var writeMapping (const BoardData::MappingData& mapping)
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty ("source", mapping.source);
    obj->setProperty ("channel", mapping.channel);
    obj->setProperty ("number", mapping.number);
    obj->setProperty ("action", mapping.action);
    obj->setProperty ("target", mapping.target);
    obj->setProperty ("min", (double) mapping.minValue);
    obj->setProperty ("max", (double) mapping.maxValue);

    return juce::var (obj);
}
} // namespace

juce::String toJson (const BoardData& board)
{
    auto* root = new juce::DynamicObject();

    root->setProperty ("schemaVersion", BoardData::kSchemaVersion);
    root->setProperty ("name", board.name);
    root->setProperty ("inputTrimDb", (double) board.inputTrimDb);
    root->setProperty ("outputTrimDb", (double) board.outputTrimDb);
    root->setProperty ("slots", asArray (board.slots, writeSlot));
    root->setProperty ("macros", asArray (board.macros, writeMacro));
    root->setProperty ("mappings", asArray (board.mappings, writeMapping));

    // Six decimal places keeps a hand-written file readable and still holds every value this app
    // stores (registry values are floats, and floats carry about seven significant digits anyway).
    return juce::JSON::toString (juce::var (root), false, 6);
}

LoadResult fromJson (const juce::String& json, BoardData& out)
{
    LoadResult result;

    juce::var parsed;
    const auto parseResult = juce::JSON::parse (json, parsed);

    if (parseResult.failed())
    {
        result.error = "not a JSON file: " + parseResult.getErrorMessage();
        return result;
    }

    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
    {
        result.error = "a board file must be a JSON object";
        return result;
    }

    const auto versionVar = root->getProperty ("schemaVersion");

    if (! isNumber (versionVar))
    {
        result.error = "the file has no \"schemaVersion\"";
        return result;
    }

    const int version = intOr (versionVar, 0);

    if (version < 1)
    {
        result.error = "\"schemaVersion\" must be 1 or more, not " + juce::String (version);
        return result;
    }

    if (version > BoardData::kSchemaVersion)
    {
        result.error = "this board was saved by a newer pump BOARD (schema " + juce::String (version)
                     + ", this build reads " + juce::String (BoardData::kSchemaVersion) + ")";
        return result;
    }

    BoardData data;
    data.name = stringOr (root->getProperty ("name"));
    data.inputTrimDb = (float) doubleOr (root->getProperty ("inputTrimDb"), 0.0);
    data.outputTrimDb = (float) doubleOr (root->getProperty ("outputTrimDb"), 0.0);

    const auto slotsVar = root->getProperty ("slots");

    if (slotsVar.isArray())
    {
        const int count = slotsVar.size();

        if (count > kMaxSlots)
            result.warnings.add ("The board has " + juce::String (count) + " slots; only the first "
                                 + juce::String (kMaxSlots) + " were loaded");

        for (int i = 0; i < juce::jmin (count, kMaxSlots); ++i)
            data.slots.push_back (readSlot (slotsVar[i], i, result.warnings));
    }
    else if (! slotsVar.isVoid())
    {
        result.warnings.add ("\"slots\" is not a list; the board is empty");
    }

    const auto macrosVar = root->getProperty ("macros");

    if (macrosVar.isArray())
    {
        const int count = macrosVar.size();

        if (count > kMaxMacros)
            result.warnings.add ("The file has " + juce::String (count) + " macros; only the first "
                                 + juce::String (kMaxMacros) + " were loaded");

        for (int i = 0; i < juce::jmin (count, kMaxMacros); ++i)
            data.macros.push_back (readMacro (macrosVar[i], i, result.warnings));
    }
    else if (! macrosVar.isVoid())
    {
        result.warnings.add ("\"macros\" is not a list; no macros loaded");
    }

    const auto mappingsVar = root->getProperty ("mappings");

    if (mappingsVar.isArray())
    {
        for (int i = 0; i < mappingsVar.size(); ++i)
        {
            BoardData::MappingData mapping;

            if (readMapping (mappingsVar[i], i, mapping, result.warnings))
                data.mappings.push_back (std::move (mapping));
        }
    }
    else if (! mappingsVar.isVoid())
    {
        result.warnings.add ("\"mappings\" is not a list; no mappings loaded");
    }

    out = std::move (data);
    result.ok = true;
    return result;
}
} // namespace pb
