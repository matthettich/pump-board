#pragma once
// Card 5.24: how much of each block's time budget a slot spends, and the board's total. The number is measured in
// Slot::process around the effect's own call, smoothed, and published to a fixed table here for the UI to read.
//
// Why a side table: `Slot.h` is a fixed scaffold header ("do not change without review"), so the timing state lives
// outside it, keyed by the slot's address — the same work-around MidiRouter uses for its retire list. The table is
// fixed-size and lock-free: registration happens on the message thread, publishing on the audio thread, and a read
// is a linear scan of at most 64 atomics.
namespace pb
{
class Slot;

namespace load
{
// Message thread: Slot's constructor and destructor.
void registerSlot (const Slot* slot) noexcept;
void unregisterSlot (const Slot* slot) noexcept;

// Message thread, from Slot::prepare: the rate the block budget is computed against.
void setSampleRate (const Slot* slot, double sampleRate) noexcept;

// AUDIO THREAD. No allocation, no locks. `elapsedSeconds` is the effect's own process() time.
void publish (const Slot* slot, double elapsedSeconds, int numSamples) noexcept;

// Any thread. The smoothed fraction of the block's budget (0.25 = a quarter of the time available), 0 when unknown.
float slotLoad (const Slot* slot) noexcept;

// Any thread. The sum over the live slots — the chain's total effect cost, which is what the status line shows.
float boardLoad() noexcept;

// Message thread, for tests and for a fresh device.
void resetLoads() noexcept;

// How many slots the table can track at once; the board has sixteen, so this is generous.
constexpr int kMaxSlots = 64;
} // namespace load
} // namespace pb
