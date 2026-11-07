#include "Control/LearnController.h"
#include <algorithm>

namespace pb
{
LearnController::LearnController (ParameterRegistry& registryIn, MidiRouter& midiIn, KeyRouter& keysIn)
    : registry (registryIn), midi (midiIn), keys (keysIn)
{
    // The controller owns this half of the handshake: whoever builds a controller gets key learns, and the
    // window cannot forget to connect it. MIDI arrives through the router's FIFO instead, drained by the
    // window's timer (the audio thread is the FIFO's only producer).
    keys.onLearnCaptured = [this] (int keyCode)
    {
        sourceCaptured ({ SourceType::Key, 0, keyCode });
    };
}

LearnController::~LearnController()
{
    keys.onLearnCaptured = nullptr;
}

void LearnController::beginLearn (int paramIndex)
{
    if (paramIndex == ParameterRegistry::kInvalid)
        return;

    discardPending();                    // an earlier capture that was never answered is abandoned

    armedParamIndex = paramIndex;
    learning = true;

    midi.beginLearn();
    keys.beginLearn();

    notify();
}

void LearnController::cancelLearn()
{
    learning = false;
    armedParamIndex = ParameterRegistry::kInvalid;
    pending = false;
    pendingParam = ParameterRegistry::kInvalid;

    midi.cancelLearn();
    keys.cancelLearn();

    notify();
}

void LearnController::sourceCaptured (const MappingSource& source)
{
    if (! learning || armedParamIndex == ParameterRegistry::kInvalid)
        return;

    // Whichever source won, the other must stop waiting for a message that will never come.
    learning = false;
    midi.cancelLearn();
    keys.cancelLearn();

    pending = true;
    pendingSourceValue = source;
    pendingParam = armedParamIndex;
    armedParamIndex = ParameterRegistry::kInvalid;

    if (onPendingSource != nullptr)
        onPendingSource();

    notify();
}

ActionType LearnController::defaultActionFor (int paramIndex) const
{
    return registry.info (paramIndex).isToggle ? ActionType::Toggle : ActionType::SetValue;
}

void LearnController::commitPending (ActionType action)
{
    if (! pending || pendingParam == ParameterRegistry::kInvalid)
        return;

    const int index = pendingParam;
    const auto& info = registry.info (index);

    Mapping mapping;
    mapping.source = pendingSourceValue;
    mapping.action = action;
    mapping.paramIndex = index;
    mapping.minValue = juce::jmin (info.minValue, info.maxValue);
    mapping.maxValue = juce::jmax (info.minValue, info.maxValue);

    // Learning the same source onto the same target twice replaces the row: two identical Toggles on one
    // footswitch would cancel each other out.
    list.erase (std::remove_if (list.begin(), list.end(),
                                [&mapping] (const Mapping& existing)
                                {
                                    return existing.paramIndex == mapping.paramIndex
                                        && existing.source.type == mapping.source.type
                                        && existing.source.number == mapping.source.number
                                        && existing.source.channel == mapping.source.channel;
                                }),
                list.end());

    list.push_back (mapping);

    pending = false;
    pendingParam = ParameterRegistry::kInvalid;

    publishTable();
    notify();
}

void LearnController::discardPending()
{
    if (! pending)
        return;

    pending = false;
    pendingParam = ParameterRegistry::kInvalid;
    notify();
}

void LearnController::removeMapping (int index)
{
    if (index < 0 || index >= (int) list.size())
        return;

    list.erase (list.begin() + index);
    publishTable();
    notify();
}

void LearnController::setMappingAction (int index, ActionType action)
{
    if (index < 0 || index >= (int) list.size())
        return;

    list[(size_t) index].action = action;
    publishTable();
    notify();
}

void LearnController::setMappingRange (int index, float minValue, float maxValue)
{
    if (index < 0 || index >= (int) list.size())
        return;

    if (! std::isfinite (minValue) || ! std::isfinite (maxValue))
        return;

    list[(size_t) index].minValue = juce::jmin (minValue, maxValue);
    list[(size_t) index].maxValue = juce::jmax (minValue, maxValue);

    publishTable();
    notify();
}

void LearnController::clearAll()
{
    if (list.empty())
        return;

    list.clear();
    publishTable();
    notify();
}

void LearnController::setMappings (std::vector<Mapping> newList)
{
    list = std::move (newList);
    publishTable();
    notify();
}

void LearnController::publishTable()
{
    auto table = std::make_unique<MappingTable>();
    table->mappings = list;
    midi.publish (std::move (table));
}

void LearnController::notify() const
{
    if (onChange != nullptr)
        onChange();
}

juce::String LearnController::describeSource (const MappingSource& source) const
{
    const auto channel = source.channel == 0 ? juce::String ("ANY") : juce::String ((int) source.channel);

    switch (source.type)
    {
        case SourceType::Note:  return "NOTE " + juce::String (source.number) + "  CH " + channel;
        case SourceType::CC:    return "CC " + juce::String (source.number) + "  CH " + channel;
        case SourceType::ProgramChange: return "PROGRAM " + juce::String (source.number);
        case SourceType::Key:
            if (source.number >= 32 && source.number < 127)
                return "KEY " + juce::String::charToString ((juce::juce_wchar) source.number).toUpperCase();

            return "KEY " + juce::String (source.number);
        default: break;
    }

    return "-";
}

juce::String LearnController::describeAction (ActionType action) const
{
    switch (action)
    {
        case ActionType::Toggle:        return "TOGGLE";
        case ActionType::Momentary:     return "MOMENTARY";
        case ActionType::CCThreshold:   return "CC 64";
        case ActionType::SetValue:      return "VALUE";
        case ActionType::PresetSelect:  return "PRESET";
        case ActionType::PresetNext:    return "NEXT BOARD";
        case ActionType::PresetPrev:    return "PREV BOARD";
        default: break;
    }

    return "-";
}

juce::String LearnController::describeTarget (int paramIndex) const
{
    if (paramIndex == ParameterRegistry::kInvalid)
        return "-";

    if (targetLabel != nullptr)
        if (const auto label = targetLabel (paramIndex); label.isNotEmpty())
            return label;

    const auto name = registry.info (paramIndex).displayName;
    return name.isNotEmpty() ? name.toUpperCase() : registry.info (paramIndex).id.toUpperCase();
}
} // namespace pb
