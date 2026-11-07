#include "Engine/LoadMeter.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>

namespace pb
{
namespace load
{
namespace
{
struct Entry
{
    std::atomic<const Slot*> slot { nullptr };
    std::atomic<float> load { 0.0f };
    std::atomic<double> rate { 48000.0 };
};

std::array<Entry, kMaxSlots> entries;

// A linear scan, never a map: at most 64 atomic loads, on a thread that must not allocate.
int find (const Slot* slot) noexcept
{
    // A null slot is not "the first free entry": every free entry has a null pointer, so without this guard a
    // publish with no slot at all would write into an empty row and read back as a slot's load.
    if (slot == nullptr)
        return -1;

    for (int i = 0; i < kMaxSlots; ++i)
        if (entries[(size_t) i].slot.load (std::memory_order_acquire) == slot)
            return i;

    return -1;
}
} // namespace

void registerSlot (const Slot* slot) noexcept
{
    if (slot == nullptr || find (slot) >= 0)
        return;

    for (auto& entry : entries)
    {
        if (entry.slot.load (std::memory_order_acquire) == nullptr)
        {
            entry.load.store (0.0f, std::memory_order_relaxed);
            entry.slot.store (slot, std::memory_order_release);
            return;
        }
    }
}

void unregisterSlot (const Slot* slot) noexcept
{
    const int i = find (slot);

    if (i >= 0)
    {
        entries[(size_t) i].load.store (0.0f, std::memory_order_relaxed);
        entries[(size_t) i].slot.store (nullptr, std::memory_order_release);
    }
}

void setSampleRate (const Slot* slot, double sampleRate) noexcept
{
    const int i = find (slot);

    if (i >= 0)
        entries[(size_t) i].rate.store (sampleRate > 0.0 ? sampleRate : 48000.0, std::memory_order_relaxed);
}

void publish (const Slot* slot, double elapsedSeconds, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int i = find (slot);

    if (i < 0)
        return;

    const double rate = entries[(size_t) i].rate.load (std::memory_order_relaxed);
    const double budget = (double) numSamples / (rate > 0.0 ? rate : 48000.0);

    if (budget <= 0.0)
        return;

    const float fraction = juce::jlimit (0.0f, 4.0f, (float) (elapsedSeconds / budget));
    const float previous = entries[(size_t) i].load.load (std::memory_order_relaxed);

    // 90/10: a single spike should be visible but not make the bar twitch.
    entries[(size_t) i].load.store (previous * 0.9f + fraction * 0.1f, std::memory_order_relaxed);
}

float slotLoad (const Slot* slot) noexcept
{
    const int i = find (slot);
    return i >= 0 ? entries[(size_t) i].load.load (std::memory_order_relaxed) : 0.0f;
}

float boardLoad() noexcept
{
    float total = 0.0f;

    for (const auto& entry : entries)
        if (entry.slot.load (std::memory_order_acquire) != nullptr)
            total += entry.load.load (std::memory_order_relaxed);

    return total;
}

void resetLoads() noexcept
{
    for (auto& entry : entries)
        entry.load.store (0.0f, std::memory_order_relaxed);
}
} // namespace load
} // namespace pb
