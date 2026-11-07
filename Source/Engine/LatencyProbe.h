#pragma once
// Card 0.4: the real round trip, measured. The app puts one click on both outputs, records input 1 for half a
// second, and finds the click coming back — which needs an output patched into an input with a cable.
//
// It does not touch BoardProcessor: the probe rides the device as a second callback, so it sees the raw
// input and can add the click after the chain has already written its block. Every buffer it uses is
// allocated in prepare(), on the message thread, before a single block can see it.
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace pb
{
class LatencyProbe
{
public:
    LatencyProbe() = default;              // the leak detector deletes the copy constructor, which would
                                           // otherwise suppress this one

    static constexpr double kDefaultWindowSeconds = 0.5;

    // Message thread, and only while no block is being processed (call it before adding the callback).
    void prepare (double sampleRate, int maxBlockSize, double windowSeconds = kDefaultWindowSeconds);
    bool isPrepared() const noexcept { return sampleRate > 0.0 && record.getNumSamples() > 0; }

    // Message thread. Arm: the next block writes the click and starts recording.
    void begin();
    void cancel();

    bool isRunning() const noexcept { return running.load (std::memory_order_acquire); }
    bool isFinished() const noexcept { return finished.load (std::memory_order_acquire); }
    float resultMs() const noexcept { return result.load (std::memory_order_relaxed); }

    // Message thread, once isFinished(). Peak search over what was recorded; -1 when nothing came back.
    float finish();

    // AUDIO THREAD. Adds the click to the outputs on the armed block, then copies input 1 into the record
    // buffer until the window is full. No allocation, no locks, no I/O.
    void process (const float* const* input, int numInputChannels,
                  float* const* output, int numOutputChannels, int numSamples) noexcept;

    // Pure, tested directly.
    static float peakOf (const float* samples, int numSamples) noexcept;
    static int findClick (const float* samples, int numSamples, float threshold) noexcept;

    // Below this, nothing useful came back: a quiet instrument is not a click.
    static constexpr float kMinPeak = 0.02f;

private:
    static constexpr int kClickSamples = 4;
    static constexpr float kClickValue = 0.85f;

    juce::AudioBuffer<float> record;          // one channel: input 1 is what a loopback cable feeds
    double sampleRate = 0.0;

    std::atomic<bool> running { false };      // armed or recording
    std::atomic<bool> clickPending { false };
    std::atomic<bool> finished { false };     // the window is full; the message thread may read `record`
    std::atomic<float> result { -1.0f };
    std::atomic<int> recorded { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatencyProbe)
};
} // namespace pb
