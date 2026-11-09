#pragma once
// MIDI clock as the board's tempo (card 5.32). The tempo is device-wide (card 5.25), so a running clock only moves
// `board/tempo`; nothing is stored in a board. System real-time messages are not notes or CCs, so this sits beside
// MidiRouter rather than inside it — and MidiRouter.h is a fixed scaffold header, which is the other reason.
//
// Audio thread only, allocation-free: the registry index is cached, the state is a plain file-scope struct, and the
// tempo is written at most once per beat and only when it moves by half a BPM.
#include "Control/ParameterRegistry.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace pb::clock
{
// Reads clock / start / continue / stop out of one block and updates `board/tempo`. `numSamples` is the block the
// buffer describes. Returns true when the registry was written.
// `tempoIndex` is the registry index of `board/tempo`. The audio thread passes the one it already holds; without it the
// clock looks the id up itself, once per registry, which is a string search over a table the message thread may be
// editing and a heap-built String - fine for a test, not for a block.
bool observe (const juce::MidiBuffer& midi, int numSamples, ParameterRegistry& registry, double sampleRate,
              int tempoIndex = ParameterRegistry::kInvalid) noexcept;

// Forgets the interval history and drops out of "running". The tempo stays where it was: a clock that stops should
// not rewind the room to 120.
void reset() noexcept;

// True once a clock has arrived and no stop has been seen since.
bool isRunning() noexcept;

// The plugin's host can supply a tempo of its own. While it does, a running MIDI clock keeps measuring and keeps
// reporting isRunning(), but must not write `board/tempo`: the host wins, and two sources writing the same value
// every beat is a fight rather than a feature (card 8.6).
void setHostTempoOwns (bool owns) noexcept;

// The tempo the last full beat implied, before the registry is written; 0 when no beat has completed yet. The
// status line and the tests use it.
double lastMeasuredBpm() noexcept;
} // namespace pb::clock
