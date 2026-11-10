#pragma once
// Card 8.2: the AudioProcessor a host sees. It owns the session and does nothing to the audio itself: the board is
// BoardProcessor, and this is the plug socket around it. Cards 8.3–8.7 fill in state, parameters, the editor, host
// sync and programs.
#include "Plugin/PluginSession.h"
#include "Plugin/PluginParameters.h"
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
    // Card 8.6: SPRING's decay reaches 8 s (T60) and every other tail is shorter. A host uses this to decide how long
    // to keep rendering after a stop, so zero would cut a reverb off mid-flight.
    double getTailLengthSeconds() const override { return 8.0; }

    // The host's own bypass is Board Bypass. A wrapper that is given a bypass parameter always calls processBlock and
    // never processBlockBypassed, so the board's 10 ms fade and its latency-matched dry copy do the bypassing, not the
    // host's cut to dry. The parameter id (`board/bypass`) is the one it always had, so saved projects still find it.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;      // card 8.5
    bool hasEditor() const override { return true; }

    int getNumPrograms() override { return 1; }                               // card 8.7
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    // Card 8.3: the host's own save and restore, called on the host's main thread.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    PluginSession& getSession() noexcept { return session; }
    PluginParameters& getAutomation() noexcept { return parameters; }

private:
    void timerCallback() override;
    void beginStateRestore (const juce::var& state);
    void landPendingState (bool force);  // applies a waiting restore if its fade is over; `force` ignores the due time (no audio to fade)
    void setBoardBypassedQuietly (bool); // the restore's own fade: not announced to the host
    void afterBoardRestored();
    void notifyHostOfChainChange();      // message thread, or handed to it when a host restores off-thread
    bool updateHostTempo();
    int chainLatencySamples();

    PluginSession session;
    PluginParameters parameters;    // card 8.4: the automation surface, built from the session's own registry

    // A restore waits one fade behind the bypass, so the swap never lands on a live chain. Guarded: a host may call
    // setStateInformation from a thread of its own while the message thread's timer lands the one before it.
    juce::CriticalSection pendingLock;
    juce::var pendingState;
    juce::int64 pendingStateDueMs = 0;
    bool pendingStateUnbypass = false;

    int tempoIndex = ParameterRegistry::kInvalid;
    int reportedLatency = 0;

    // A host may prepare on another thread; the timer has to be started on this one, and this is what lets the
    // message thread check the processor is still alive before touching it.
    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginProcessor)
};
} // namespace pb
