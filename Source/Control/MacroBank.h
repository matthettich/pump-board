#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include "Control/ParameterRegistry.h"
#include <array>
#include <vector>

namespace pb
{
// 8 macro knobs. Each macro is itself a registry param ("macro/<n>", 0..1) so it can be
// MIDI-mapped like anything else. When its value changes, it writes its targets.
class MacroBank
{
public:
    static constexpr int kNumMacros = 8;

    enum class Curve : uint8_t { Linear, Exponential, Logarithmic, Inverted };

    struct Target
    {
        int paramIndex = ParameterRegistry::kInvalid;
        float minValue = 0.0f, maxValue = 1.0f;   // in the target's own units
        Curve curve = Curve::Linear;
    };

    explicit MacroBank (ParameterRegistry& registry); // registers macro/0..7

    // Message thread.
    void setTargets (int macro, std::vector<Target> targets);
    const std::vector<Target>& getTargets (int macro) const;
    void setName (int macro, const juce::String& name);
    juce::String getName (int macro) const;
    int macroParamIndex (int macro) const noexcept { return macroIndices[(size_t) macro]; }

    // Message thread, from the ~30 Hz timer: if a macro's value changed since last call,
    // write all its targets. (Macros moved by MIDI are picked up here too.)
    void update();

    // Pure function, tested directly.
    static float mapValue (float macro01, const Target& t) noexcept;

private:
    ParameterRegistry& registry;
    std::array<int, kNumMacros> macroIndices {};
    std::array<float, kNumMacros> lastValues {};
    std::array<std::vector<Target>, kNumMacros> targets;
    std::array<juce::String, kNumMacros> names;
};
} // namespace pb
