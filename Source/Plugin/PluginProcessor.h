#pragma once
// Card 8.2: the AudioProcessor a host sees. It owns the session and does nothing to the audio itself: the board is
// BoardProcessor, and this is the plug socket around it. Cards 8.3–8.7 fill in state, parameters, the editor, host
// sync and programs.
#include "Plugin/PluginSession.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

namespace pb
{
class PluginProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    const juce::String getName() const override { return "pump BOARD"; }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }   // card 8.5
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }                               // card 8.7
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    // Card 8.3: the host's own save and restore, called on the host's main thread.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    PluginSession& getSession() noexcept { return session; }

private:
    void timerCallback() override;
    void beginStateRestore (const juce::var& state);

    PluginSession session;

    // A restore waits one fade behind the bypass, so the swap never lands on a live chain.
    juce::var pendingState;
    juce::int64 pendingStateDueMs = 0;
    bool pendingStateUnbypass = false;
};
} // namespace pb
