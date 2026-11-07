#pragma once
// Card 2.3: the board's safety limiter. Always on, never mappable, the last thing the signal passes.
//
// Peak, stereo-linked, no look-ahead: the sample that goes over the ceiling has already been reduced by the time
// it is written, so the output cannot overshoot. The price of no look-ahead is that a transient is shaped rather
// than turned down early, which is the right trade for a board of pedals nobody asked to be protected from.
// Release is 50 ms, so a loud chord ducks the whole board briefly instead of pumping on every note.
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace pb
{
class SafetyLimiter
{
public:
    static constexpr float kCeilingDb = -0.3f;

    void prepare (double sampleRate) noexcept
    {
        ceiling = juce::Decibels::decibelsToGain (kCeilingDb);
        releaseCoef = 1.0f - std::exp (-1.0f / (float) juce::jmax (1.0, 0.050 * sampleRate));
        gain = 1.0f;
    }

    void reset() noexcept { gain = 1.0f; }

    // AUDIO THREAD, in place. Never allocates. `right` may be nullptr for a mono buffer.
    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float peak = std::abs (left[i]);

            if (right != nullptr)
                peak = juce::jmax (peak, std::abs (right[i]));

            if (! std::isfinite (peak))
            {
                // A pedal produced a NaN or an infinity; do not let it poison the rest of the board.
                left[i] = 0.0f;

                if (right != nullptr)
                    right[i] = 0.0f;

                gain = 1.0f;
                continue;
            }

            const float target = peak > ceiling ? ceiling / peak : 1.0f;

            if (target < gain)
                gain = target;                      // instant attack
            else
                gain += (target - gain) * releaseCoef;

            // The multiply lands on the ceiling exactly bar rounding; the clamp makes that an invariant.
            left[i] = juce::jlimit (-ceiling, ceiling, left[i] * gain);

            if (right != nullptr)
                right[i] = juce::jlimit (-ceiling, ceiling, right[i] * gain);
        }
    }

    float currentGain() const noexcept { return gain; }

private:
    float ceiling = 0.9661f;
    float releaseCoef = 0.0004f;
    float gain = 1.0f;
};
} // namespace pb
