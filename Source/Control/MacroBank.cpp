#include "Control/MacroBank.h"
#include <cmath>

namespace pb
{
namespace
{
// Writes one macro's targets for a value in 0..1. Used both when the knob moves and when a target is added,
// so a fresh target lands where the macro already is instead of waiting for the next twitch.
void writeTargets (ParameterRegistry& registry, const std::vector<MacroBank::Target>& targets, float macro01) noexcept
{
    for (const auto& target : targets)
        if (target.paramIndex != ParameterRegistry::kInvalid)
            registry.set (target.paramIndex, MacroBank::mapValue (macro01, target));
}
} // namespace

MacroBank::MacroBank (ParameterRegistry& registryIn)
    : registry (registryIn)
{
    for (int i = 0; i < kNumMacros; ++i)
    {
        ParameterRegistry::Entry entry;
        entry.id = "macro/" + juce::String (i);
        entry.displayName = "Macro " + juce::String (i + 1);
        entry.minValue = 0.0f;
        entry.maxValue = 1.0f;
        entry.defaultValue = 0.0f;                  // a macro starts at the bottom, so it writes nothing yet

        macroIndices[(size_t) i] = registry.add (entry);
        lastValues[(size_t) i] = 0.0f;
        names[(size_t) i] = "MACRO " + juce::String (i + 1);
    }
}

void MacroBank::setTargets (int macro, std::vector<Target> newTargets)
{
    if (macro < 0 || macro >= kNumMacros)
        return;

    targets[(size_t) macro] = std::move (newTargets);

    const int index = macroIndices[(size_t) macro];
    const float value = index != ParameterRegistry::kInvalid
                          ? juce::jlimit (0.0f, 1.0f, registry.get (index))
                          : 0.0f;

    lastValues[(size_t) macro] = value;
    writeTargets (registry, targets[(size_t) macro], value);
}

const std::vector<MacroBank::Target>& MacroBank::getTargets (int macro) const
{
    static const std::vector<Target> none;

    if (macro < 0 || macro >= kNumMacros)
        return none;

    return targets[(size_t) macro];
}

void MacroBank::setName (int macro, const juce::String& name)
{
    if (macro < 0 || macro >= kNumMacros)
        return;

    names[(size_t) macro] = name.trim();
}

juce::String MacroBank::getName (int macro) const
{
    if (macro < 0 || macro >= kNumMacros)
        return {};

    const auto& name = names[(size_t) macro];
    return name.isNotEmpty() ? name : "MACRO " + juce::String (macro + 1);
}

void MacroBank::update()
{
    for (int i = 0; i < kNumMacros; ++i)
    {
        const int index = macroIndices[(size_t) i];

        if (index == ParameterRegistry::kInvalid)
            continue;

        const float value = juce::jlimit (0.0f, 1.0f, registry.get (index));

        // Only when the macro actually moved: a target edited by hand must stay where it was put.
        if (std::abs (value - lastValues[(size_t) i]) <= 1.0e-6f)
            continue;

        lastValues[(size_t) i] = value;
        writeTargets (registry, targets[(size_t) i], value);
    }
}

float MacroBank::mapValue (float macro01, const Target& target) noexcept
{
    const float v = juce::jlimit (0.0f, 1.0f, macro01);
    float shaped = v;

    switch (target.curve)
    {
        case Curve::Linear:        shaped = v;             break;
        case Curve::Exponential:   shaped = v * v;         break;
        case Curve::Logarithmic:   shaped = std::sqrt (v); break;
        case Curve::Inverted:      shaped = 1.0f - v;      break;
        default: break;
    }

    return target.minValue + shaped * (target.maxValue - target.minValue);
}
} // namespace pb
