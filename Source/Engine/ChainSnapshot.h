#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include "Engine/Slot.h"
#include <array>
#include <memory>

namespace pb
{
// Immutable once published. Built on the message thread, read on the audio thread.
// Holds shared_ptrs so a slot stays alive while any snapshot still points at it.
// The audio thread only ever touches the raw Slot* in `order`, never the shared_ptrs.
struct ChainSnapshot
{
    static constexpr int kMaxSlots = 16;

    std::array<std::shared_ptr<Slot>, kMaxSlots> owned {};
    std::array<Slot*, kMaxSlots> order {};    // order[i] == owned[i].get()
    int count = 0;

    // Message thread helper. Returns false if already full.
    bool push (std::shared_ptr<Slot> slot)
    {
        if (count >= kMaxSlots) return false;
        order[(size_t) count] = slot.get();
        owned[(size_t) count] = std::move (slot);
        ++count;
        return true;
    }
};
} // namespace pb
