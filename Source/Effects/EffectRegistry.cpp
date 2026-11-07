#include "Effects/EffectRegistry.h"
#include <algorithm>

namespace pb
{
EffectRegistry& EffectRegistry::instance()
{
    static EffectRegistry registry;
    return registry;
}

void EffectRegistry::add (EffectDescriptor descriptor)
{
    jassert (descriptors.find (descriptor.typeId) == descriptors.end());   // one descriptor per typeId

    descriptors[descriptor.typeId] = std::move (descriptor);
}

EffectPtr EffectRegistry::create (const juce::String& typeId) const
{
    const auto found = descriptors.find (typeId);

    // Unknown ids return nullptr and say nothing: this is also the path taken while loading a file that
    // names an effect this build does not have.
    if (found == descriptors.end() || found->second.create == nullptr)
        return nullptr;

    return found->second.create();
}

bool EffectRegistry::contains (const juce::String& typeId) const
{
    return descriptors.find (typeId) != descriptors.end();
}

std::vector<EffectDescriptor> EffectRegistry::list() const
{
    std::vector<EffectDescriptor> result;
    result.reserve (descriptors.size());

    for (const auto& entry : descriptors)
        result.push_back (entry.second);

    std::sort (result.begin(), result.end(), [] (const EffectDescriptor& a, const EffectDescriptor& b)
    {
        if (const int byCategory = a.category.compareIgnoreCase (b.category); byCategory != 0)
            return byCategory < 0;

        return a.displayName.compareIgnoreCase (b.displayName) < 0;
    });

    return result;
}
} // namespace pb
