#include "Control/MidiClock.h"
#include <cmath>

namespace pb::clock
{
namespace
{
constexpr int kClocksPerBeat = 24;            // the MIDI beat clock's own rate: 24 pulses per quarter note
constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;
constexpr double kSmoothing = 0.35;           // per beat, so a clock's own jitter does not show in the number
constexpr float kWriteThreshold = 0.5f;       // BPM: below this a new beat is not worth a registry write

struct State
{
    const ParameterRegistry* registeredWith = nullptr;   // the registry the cached index belongs to
    int tempoIndex = ParameterRegistry::kInvalid;

    double sampleRate = 48000.0;
    juce::int64 samplePos = 0;        // samples since the clock last started
    juce::int64 lastClockPos = -1;    // -1: no clock yet
    juce::int64 beatStartPos = -1;
    int clocksThisBeat = 0;

    double measured = 0.0;            // the last beat's own BPM, unsmoothed
    double smoothed = 0.0;            // 0: nothing measured yet
    float written = -1.0f;            // the value this clock last put in the registry
    bool running = false;
    bool hostOwnsTempo = false;       // the plugin's host reports a tempo; this clock must not write (card 8.6)
};

State state;                          // file scope: no initialisation guard on the audio thread
} // namespace

void reset() noexcept
{
    auto& s = state;

    s.samplePos = 0;
    s.lastClockPos = -1;
    s.beatStartPos = -1;
    s.clocksThisBeat = 0;
    s.measured = 0.0;
    s.smoothed = 0.0;
    s.written = -1.0f;
    s.running = false;
}

bool isRunning() noexcept { return state.running; }

double lastMeasuredBpm() noexcept { return state.measured; }

bool observe (const juce::MidiBuffer& midi, int numSamples, ParameterRegistry& registry, double sampleRate) noexcept
{
    auto& s = state;
    s.sampleRate = sampleRate > 0.0 ? sampleRate : s.sampleRate;

    bool wrote = false;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isMidiStart() || message.isMidiContinue())
        {
            reset();
            state.running = true;
            continue;
        }

        if (message.isMidiStop())
        {
            reset();
            continue;
        }

        if (! message.isMidiClock())
            continue;

        state.running = true;

        const juce::int64 pos = state.samplePos + (juce::int64) metadata.samplePosition;

        if (state.lastClockPos >= 0)
        {
            const juce::int64 interval = pos - state.lastClockPos;

            // Two seconds between clocks is not a slow tempo, it is a clock that stopped: start the beat again
            // rather than average a pause into it. Two clocks on the same sample (a coalesced USB burst) are still two
            // ticks of the beat, not a restart: only time running backwards restarts it.
            if (interval < 0 || interval > (juce::int64) (2.0 * state.sampleRate))
            {
                state.beatStartPos = pos;
                state.clocksThisBeat = 0;
                state.smoothed = 0.0;
            }
            else if (state.beatStartPos < 0)
            {
                state.beatStartPos = pos;
                state.clocksThisBeat = 0;
            }
            else if (++state.clocksThisBeat >= kClocksPerBeat)
            {
                const auto beat = pos - state.beatStartPos;

                if (beat > 0)
                {
                    const double bpm = juce::jlimit (kMinBpm, kMaxBpm,
                                                     60.0 * state.sampleRate / (double) beat);

                    state.measured = bpm;
                    state.smoothed = state.smoothed <= 0.0 ? bpm
                                                           : state.smoothed + kSmoothing * (bpm - state.smoothed);

                    // The index is looked up once per registry, never per beat: find() compares strings, which is
                    // not something to do on the audio thread every half second.
                    if (state.registeredWith != &registry)
                    {
                        state.registeredWith = &registry;
                        state.tempoIndex = registry.find ("board/tempo");
                    }

                    const auto target = (float) state.smoothed;

                    if (! state.hostOwnsTempo
                        && state.tempoIndex != ParameterRegistry::kInvalid
                        && (state.written < 0.0f || std::abs (target - state.written) >= kWriteThreshold))
                    {
                        registry.set (state.tempoIndex, target);
                        state.written = target;
                        wrote = true;
                    }
                }

                state.beatStartPos = pos;
                state.clocksThisBeat = 0;
            }
        }
        else
        {
            state.beatStartPos = pos;
            state.clocksThisBeat = 0;
        }

        state.lastClockPos = pos;
    }

    if (numSamples > 0)
        state.samplePos += (juce::int64) numSamples;

    return wrote;
}

void setHostTempoOwns (bool owns) noexcept
{
    state.hostOwnsTempo = owns;

    // Forget what this clock last wrote, so a clock that takes the tempo back from a host is not silent just because
    // the number it measures happens to match. The flag itself survives reset(): it is a mode, not clock state.
    state.written = -1.0f;
}
} // namespace pb::clock
