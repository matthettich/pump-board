#pragma once
// SCAFFOLD — fixed interface. Do not change without review.
#include "Engine/ChainSnapshot.h"
#include "Engine/SafetyLimiter.h"
#include "Control/ParameterRegistry.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

namespace pb
{
class MidiRouter;

// The one AudioProcessor the app plays through an AudioProcessorPlayer.
class BoardProcessor final : public juce::AudioProcessor
{
public:
    BoardProcessor (ParameterRegistry& registry, MidiRouter& midiRouter);
    ~BoardProcessor() override;

    // ---- Message thread ----
    // Takes ownership. The previous snapshot moves to the retire list.
    void publish (std::unique_ptr<ChainSnapshot> next);
    // Frees retired snapshots the audio thread can no longer be using.
    // Call from a ~30 Hz timer on the message thread.
    void collectGarbage();

    // ---- juce::AudioProcessor ----
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    // AUDIO THREAD:
    //   1. blocksProcessed++ (after reading the snapshot pointer)
    //   2. midiRouter.handle (midi)
    //   3. mono input? copy L to R
    //   4. input trim, then each slot in order, then output trim
    //   5. board bypass crossfade, then the safety limiter (always on)
    //   6. update the peak meters
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    const juce::String getName() const override { return "pump BOARD"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Meters (any thread).
    float inputPeak() const noexcept { return inPeak.load (std::memory_order_relaxed); }
    float outputPeak() const noexcept { return outPeak.load (std::memory_order_relaxed); }

    double getSampleRateHz() const noexcept { return sampleRateHz; }
    int getMaxBlock() const noexcept { return maxBlock; }

private:
    ParameterRegistry& registry;
    MidiRouter& midiRouter;

    std::atomic<ChainSnapshot*> active { nullptr };
    std::atomic<uint64_t> blocksProcessed { 0 };

    struct Retired { ChainSnapshot* snapshot; uint64_t retiredAtBlock; };
    std::vector<Retired> retireList;            // message thread only

    int bypassIndex = ParameterRegistry::kInvalid;
    int inputTrimIndex = ParameterRegistry::kInvalid;
    int outputTrimIndex = ParameterRegistry::kInvalid;
    int tempoIndex = ParameterRegistry::kInvalid;     // "board/tempo", in BPM
    double lastTempo = -1.0;                          // audio thread only; -1 forces the first push

    // Card 5.4: the dry copy for the board bypass is a delay ring, read `chainLatency` frames later, so bypassing
    // a board with latency-reporting pedals on it stitches two copies of the same moment together. (A ring needs a
    // position, which is why this scaffold header gained one int; see NOTES.md, card 5.4.)
    juce::AudioBuffer<float> dryBuffer;         // sized in prepareToPlay: one block plus the delay ring
    int dryWritePos = 0;                        // audio thread only
    SafetyLimiter limiter;                      // card 2.3: always on, the last stage
    juce::SmoothedValue<float> boardWet, inGain, outGain;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    double sampleRateHz = 48000.0;
    int maxBlock = 512;
};
} // namespace pb
