#pragma once
// Card 5.23: pump's own name arrays, turned into the choices a ParamInfo can carry. Every array below lives in a
// pump params header the adapter already includes; this keeps the adapters from hand-copying names (and from
// drifting when pump adds one). Not a pump file, not a scaffold interface.
#include "Effects/IEffect.h"
#include <vector>

namespace pb
{
namespace choices
{
// N names, in the order the parameter's values run. The caller passes pump's array and pump's count, so a count
// that disagrees with the values shows up in ParamChoicesTests rather than at runtime.
inline std::vector<juce::String> names (const char* const* array, int count)
{
    std::vector<juce::String> out;
    out.reserve ((size_t) juce::jmax (0, count));

    for (int i = 0; i < count; ++i)
        out.push_back (juce::String (array[i]));

    return out;
}
} // namespace choices
} // namespace pb
