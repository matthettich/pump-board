#pragma once
// pump BOARD — keeping NaN and infinity out of the pedals.
//
// A single non-finite sample poisons any pedal with feedback (springs, reverbs, delays, filters) for good: its
// state goes NaN, the safety limiter turns the output into silence, and only a fresh pedal brings it back. Found
// 7 Nov 2026: TWANG went silent after one NaN at the input, which is what a Mac audio device can deliver in its
// first buffer after starting. So the board scrubs its input and every slot scrubs its own output: every pedal
// only ever sees finite samples.
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace pb
{
// AUDIO THREAD, in place, never allocates. Non-finite samples become 0. Returns true if any were found.
inline bool scrubNonFinite (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
{
    bool found = false;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (data[i]))
            {
                data[i] = 0.0f;
                found = true;
            }
        }
    }

    return found;
}
} // namespace pb
