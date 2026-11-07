#include "Control/ParameterRegistry.h"
#include <cmath>

namespace pb
{
namespace
{
const ParameterRegistry::Entry& emptyEntry()
{
    static const ParameterRegistry::Entry empty;
    return empty;
}
} // namespace

int ParameterRegistry::add (const Entry& entry)
{
    if (entry.id.isEmpty())
        return kInvalid;

    if (const int existing = find (entry.id); existing != kInvalid)
        return existing;

    for (int i = 0; i < kMaxParams; ++i)
    {
        if (entries[(size_t) i].inUse)
            continue;

        entries[(size_t) i] = entry;
        entries[(size_t) i].inUse = true;

        // Written before the index is handed out; from then on read-only, so the audio thread may read them raw.
        const float lo = juce::jmin (entry.minValue, entry.maxValue);
        const float hi = juce::jmax (entry.minValue, entry.maxValue);
        minCache[(size_t) i].store (lo, std::memory_order_relaxed);
        maxCache[(size_t) i].store (hi, std::memory_order_relaxed);

        values[(size_t) i].store (juce::jlimit (lo, hi, entry.defaultValue), std::memory_order_relaxed);
        return i;
    }

    return kInvalid;
}

void ParameterRegistry::remove (int index)
{
    if (index < 0 || index >= kMaxParams)
        return;

    if (! entries[(size_t) index].inUse)
        return;

    entries[(size_t) index] = Entry {};          // inUse = false: the slot is free for reuse
    values[(size_t) index].store (0.0f, std::memory_order_relaxed);
}

int ParameterRegistry::find (const juce::String& id) const
{
    if (id.isEmpty())
        return kInvalid;

    for (int i = 0; i < kMaxParams; ++i)
        if (entries[(size_t) i].inUse && entries[(size_t) i].id == id)
            return i;

    return kInvalid;
}

const ParameterRegistry::Entry& ParameterRegistry::info (int index) const
{
    if (index < 0 || index >= kMaxParams || ! entries[(size_t) index].inUse)
        return emptyEntry();

    return entries[(size_t) index];
}

void ParameterRegistry::set (int index, float value) noexcept
{
    // The header's inline get() does no bounds check, so a kInvalid index must never get past here.
    if (index < 0 || index >= kMaxParams || ! std::isfinite (value))
        return;

    values[(size_t) index].store (juce::jlimit (minCache[(size_t) index].load (std::memory_order_relaxed),
                                                     maxCache[(size_t) index].load (std::memory_order_relaxed), value),
                                  std::memory_order_relaxed);
    counter.fetch_add (1, std::memory_order_relaxed);
}

void ParameterRegistry::setNormalised (int index, float value01) noexcept
{
    if (index < 0 || index >= kMaxParams || ! std::isfinite (value01))
        return;

    const float lo = minCache[(size_t) index].load (std::memory_order_relaxed);
    const float hi = maxCache[(size_t) index].load (std::memory_order_relaxed);
    set (index, lo + juce::jlimit (0.0f, 1.0f, value01) * (hi - lo));
}

float ParameterRegistry::getNormalised (int index) const noexcept
{
    if (index < 0 || index >= kMaxParams)
        return 0.0f;

    const float lo = minCache[(size_t) index].load (std::memory_order_relaxed);
    const float hi = maxCache[(size_t) index].load (std::memory_order_relaxed);

    if (hi <= lo)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f, (get (index) - lo) / (hi - lo));
}

void ParameterRegistry::toggle (int index) noexcept
{
    if (index < 0 || index >= kMaxParams)
        return;

    const float lo = minCache[(size_t) index].load (std::memory_order_relaxed);
    const float hi = maxCache[(size_t) index].load (std::memory_order_relaxed);
    set (index, get (index) >= (lo + hi) * 0.5f ? lo : hi);
}
} // namespace pb
