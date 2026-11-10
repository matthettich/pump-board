#include "Control/MidiClock.h"
#include <cmath>

namespace pb
{
namespace
{
constexpr int kClocksPerBeat = 24;            // the MIDI beat clock's own rate: 24 pulses per quarter note
constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;
constexpr double kSmoothing = 0.35;           // per beat, so a clock's own jitter does not show in the number
constexpr float kWriteThreshold = 0.5f;       // BPM: below this a new beat is not worth a registry write
} // namespace

void MidiClock::reset() noexcept
{
    samplePos = 0;
    lastClockPos = -1;
    beatStartPos = -1;
    clocksThisBeat = 0;
    measured.store (0.0, std::memory_order_relaxed);
    smoothed = 0.0;
    written = -1.0f;
    running.store (false, std::memory_order_relaxed);
}

bool MidiClock::observe (const juce::MidiBuffer& midi, int numSamples, ParameterRegistry& registry, double rate,
                         int tempoIdx) noexcept
{
    sampleRate = rate > 0.0 ? rate : sampleRate;

    // A host-owns change made on another thread asks for the written value to be forgotten; it is applied here, where
    // `written` lives.
    if (forgetWritten.exchange (false, std::memory_order_acq_rel))
        written = -1.0f;

    bool wrote = false;

    for (const auto metadata : midi)
    {
        // Clock, start, continue and stop are one byte. Anything over four bytes (SysEx) would be heap-allocated by
        // getMessage(), which the audio thread must not do; anything over three cannot be one of these anyway.
        if (metadata.numBytes < 1 || metadata.numBytes > 3)
            continue;

        const auto message = metadata.getMessage();

        if (message.isMidiStart() || message.isMidiContinue())
        {
            reset();
            running.store (true, std::memory_order_relaxed);
            continue;
        }

        if (message.isMidiStop())
        {
            reset();
            continue;
        }

        if (! message.isMidiClock())
            continue;

        running.store (true, std::memory_order_relaxed);

        const juce::int64 pos = samplePos + (juce::int64) metadata.samplePosition;

        if (lastClockPos >= 0)
        {
            const juce::int64 interval = pos - lastClockPos;

            // Two seconds between clocks is not a slow tempo, it is a clock that stopped: start the beat again
            // rather than average a pause into it. Two clocks on the same sample (a coalesced USB burst) are still two
            // ticks of the beat, not a restart: only time running backwards restarts it.
            if (interval < 0 || interval > (juce::int64) (2.0 * sampleRate))
            {
                beatStartPos = pos;
                clocksThisBeat = 0;
                smoothed = 0.0;
            }
            else if (beatStartPos < 0)
            {
                beatStartPos = pos;
                clocksThisBeat = 0;
            }
            else if (++clocksThisBeat >= kClocksPerBeat)
            {
                const auto beat = pos - beatStartPos;

                if (beat > 0)
                {
                    const double bpm = juce::jlimit (kMinBpm, kMaxBpm,
                                                     60.0 * sampleRate / (double) beat);

                    measured.store (bpm, std::memory_order_relaxed);
                    smoothed = smoothed <= 0.0 ? bpm : smoothed + kSmoothing * (bpm - smoothed);

                    // The caller's index when it has one. Otherwise it is looked up once per registry, never per
                    // beat (find() compares strings, and the literal builds a String: neither belongs on the audio
                    // thread, which is why BoardProcessor passes its own).
                    if (tempoIdx != ParameterRegistry::kInvalid)
                    {
                        registeredWith = &registry;
                        tempoIndex = tempoIdx;
                    }
                    else if (registeredWith != &registry)
                    {
                        registeredWith = &registry;
                        tempoIndex = registry.find ("board/tempo");
                    }

                    const auto target = (float) smoothed;

                    if (! hostOwnsTempo.load (std::memory_order_relaxed)
                        && tempoIndex != ParameterRegistry::kInvalid
                        && (written < 0.0f || std::abs (target - written) >= kWriteThreshold))
                    {
                        registry.set (tempoIndex, target);
                        written = target;
                        wrote = true;
                    }
                }

                beatStartPos = pos;
                clocksThisBeat = 0;
            }
        }
        else
        {
            beatStartPos = pos;
            clocksThisBeat = 0;
        }

        lastClockPos = pos;
    }

    if (numSamples > 0)
        samplePos += (juce::int64) numSamples;

    return wrote;
}

void MidiClock::setHostTempoOwns (bool owns) noexcept
{
    // The plugin calls this on every block, so only a real change of owner may forget what this clock last wrote.
    // Forgetting on every call reset `written` each block and the write threshold never applied: a steady clock
    // wrote the tempo every beat, and each write moved the registry's change counter (review).
    const bool changed = hostOwnsTempo.exchange (owns, std::memory_order_relaxed) != owns;

    // Forget what this clock last wrote, so a clock that takes the tempo back from a host is not silent just because
    // the number it measures happens to match. The flag itself survives reset(): it is a mode, not clock state.
    if (changed)
        forgetWritten.store (true, std::memory_order_release);
}
} // namespace pb
