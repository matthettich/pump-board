#pragma once
// MIDI clock as the board's tempo (card 5.32). The tempo is device-wide (card 5.25), so a running clock only moves
// `board/tempo`; nothing is stored in a board. System real-time messages are not notes or CCs, so this sits beside
// MidiRouter rather than inside it — and MidiRouter.h is a fixed scaffold header, which is the other reason.
//
// One MidiClock per BoardProcessor, so two plugin instances in one process each measure their own clock. observe()
// and reset() run on the audio thread (tests call them from the test thread, never both at once); they are
// allocation-free: the registry index is cached, and the tempo is written at most once per beat and only when it moves
// by half a BPM. The three fields another thread reads (running, measured tempo, host-owns) are atomics.
#include "Control/ParameterRegistry.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace pb
{
class MidiClock
{
public:
    // Reads clock / start / continue / stop out of one block and updates `board/tempo`. `numSamples` is the block the
    // buffer describes. Returns true when the registry was written.
    // `tempoIndex` is the registry index of `board/tempo`. The audio thread passes the one it already holds; without
    // it the clock looks the id up itself, once per registry, which is a string search over a table the message
    // thread may be editing and a heap-built String - fine for a test, not for a block.
    bool observe (const juce::MidiBuffer& midi, int numSamples, ParameterRegistry& registry, double sampleRate,
                  int tempoIndex = ParameterRegistry::kInvalid) noexcept;

    // Forgets the interval history and drops out of "running". The tempo stays where it was: a clock that stops
    // should not rewind the room to 120.
    void reset() noexcept;

    // True once a clock has arrived and no stop has been seen since. Any thread.
    bool isRunning() const noexcept { return running.load (std::memory_order_relaxed); }

    // The plugin's host can supply a tempo of its own. While it does, a running MIDI clock keeps measuring and keeps
    // reporting isRunning(), but must not write `board/tempo`: the host wins, and two sources writing the same value
    // every beat is a fight rather than a feature (card 8.6). Any thread.
    void setHostTempoOwns (bool owns) noexcept;

    // The tempo the last full beat implied, before the registry is written; 0 when no beat has completed yet. The
    // status line and the tests use it. Any thread.
    double lastMeasuredBpm() const noexcept { return measured.load (std::memory_order_relaxed); }

private:
    const ParameterRegistry* registeredWith = nullptr;   // the registry the cached index belongs to
    int tempoIndex = ParameterRegistry::kInvalid;

    double sampleRate = 48000.0;
    juce::int64 samplePos = 0;        // samples since the clock last started
    juce::int64 lastClockPos = -1;    // -1: no clock yet
    juce::int64 beatStartPos = -1;
    int clocksThisBeat = 0;

    double smoothed = 0.0;            // 0: nothing measured yet
    float written = -1.0f;            // the value this clock last put in the registry

    std::atomic<double> measured { 0.0 };        // the last beat's own BPM, unsmoothed
    std::atomic<bool> running { false };
    std::atomic<bool> hostOwnsTempo { false };   // the plugin's host reports a tempo; this clock must not write (8.6)
    std::atomic<bool> forgetWritten { false };   // set by setHostTempoOwns from any thread; the audio thread applies it
};
} // namespace pb
