#include "Engine/LatencyProbe.h"
#include <cmath>

namespace pb
{
void LatencyProbe::prepare (double sampleRateIn, int maxBlockSize, double windowSeconds)
{
    juce::ignoreUnused (maxBlockSize);      // the record loop is bounded by the window, not by the block

    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 0.0;

    const int length = juce::jmax (1, (int) std::ceil (sampleRate * windowSeconds));
    record.setSize (1, length, false, true, false);
    record.clear();

    running.store (false, std::memory_order_release);
    clickPending.store (false, std::memory_order_release);
    finished.store (false, std::memory_order_release);
    recorded.store (0, std::memory_order_release);
    result.store (-1.0f, std::memory_order_relaxed);
}

void LatencyProbe::begin()
{
    if (! isPrepared())
        return;

    recorded.store (0, std::memory_order_release);
    result.store (-1.0f, std::memory_order_relaxed);
    finished.store (false, std::memory_order_release);
    clickPending.store (true, std::memory_order_release);
    running.store (true, std::memory_order_release);
}

void LatencyProbe::cancel()
{
    running.store (false, std::memory_order_release);
    clickPending.store (false, std::memory_order_release);
    finished.store (false, std::memory_order_release);
}

void LatencyProbe::process (const float* const* input, int numInputChannels,
                           float* const* output, int numOutputChannels, int numSamples) noexcept
{
    if (! running.load (std::memory_order_acquire) || numSamples <= 0)
        return;

    // The click goes out once, at the start of the first armed block. Adding rather than writing keeps
    // whatever the chain has already put in the buffer.
    if (clickPending.exchange (false, std::memory_order_acq_rel))
    {
        const int clickable = juce::jmin (kClickSamples, numSamples);

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (output[ch] != nullptr)
                for (int i = 0; i < clickable; ++i)
                    output[ch][i] += kClickValue;
    }

    // Then record input 1 for the window, however many blocks that takes.
    const int written = recorded.load (std::memory_order_relaxed);

    if (written >= record.getNumSamples())
    {
        finished.store (true, std::memory_order_release);
        return;
    }

    // No input (the device opened with none) records silence, so the window still runs out and the probe reports
    // "nothing came back" instead of waiting forever (review, Nov 2026).
    const int toCopy = juce::jmin (numSamples, record.getNumSamples() - written);

    if (input == nullptr || numInputChannels < 1 || input[0] == nullptr)
        record.clear (0, written, toCopy);
    else
        record.copyFrom (0, written, input[0], toCopy);

    const int total = written + toCopy;
    recorded.store (total, std::memory_order_release);

    if (total >= record.getNumSamples())
    {
        finished.store (true, std::memory_order_release);
        running.store (false, std::memory_order_release);
    }
}

float LatencyProbe::finish()
{
    if (! isFinished())
        return -1.0f;

    const int count = juce::jmin (recorded.load (std::memory_order_acquire), record.getNumSamples());
    const auto* data = record.getReadPointer (0);
    const float peak = peakOf (data, count);

    // Half the peak rather than a fixed level: the click is the loudest thing in the window by definition,
    // and this way a quiet interface does not hide it.
    const int index = peak >= kMinPeak ? findClick (data, count, peak * 0.5f) : -1;
    const float ms = index >= 0 && sampleRate > 0.0
                       ? (float) ((double) index / sampleRate * 1000.0)
                       : -1.0f;

    result.store (ms, std::memory_order_relaxed);
    return ms;
}

float LatencyProbe::peakOf (const float* samples, int numSamples) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax (peak, std::abs (samples[i]));

    return peak;
}

int LatencyProbe::findClick (const float* samples, int numSamples, float threshold) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        if (std::abs (samples[i]) >= threshold)
            return i;

    return -1;
}
} // namespace pb
