#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include "Effects/IEffect.h"
#include <functional>
#include <map>

namespace pb
{
struct EffectDescriptor
{
    juce::String typeId;      // "pump.spring"
    juce::String displayName; // "pump SPRING"
    juce::String category;    // "drive", "time", "space", "filter", "dynamics", "modulation", "utility"
    std::function<EffectPtr()> create;
};

// Message thread only.
class EffectRegistry
{
public:
    static EffectRegistry& instance();

    void add (EffectDescriptor descriptor);          // asserts on duplicate typeId
    EffectPtr create (const juce::String& typeId) const; // nullptr if unknown
    bool contains (const juce::String& typeId) const;
    std::vector<EffectDescriptor> list() const;      // sorted by category, then name

private:
    std::map<juce::String, EffectDescriptor> descriptors;
};

// Called once at startup. Each adapter adds itself here (card 1.3 and phase 5).
void registerBuiltInEffects (EffectRegistry& registry);
} // namespace pb
