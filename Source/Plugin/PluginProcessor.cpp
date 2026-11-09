#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Control/MidiClock.h"
#include <cmath>

namespace pb
{
namespace
{
// Longer than the bypass smoother's 10 ms ramp, so the board is fully dry before the slots underneath it change.
constexpr juce::int64 kStateFadeMs = 25;
} // namespace

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (session.getRegistry(), session.getMacros(), session.getModel())
{
    session.loadDefaultBoard();
    tempoIndex = session.getTempoParamIndex();

    // Card 8.4: the parameters have to exist before a host asks for them, which is now — the wrapper queries them as
    // soon as the processor is constructed — and after the default board, so the sixteen switches point at slots
    // that are really there.
    parameters.addTo (*this);
    parameters.refreshChain();
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // A guitar arrives mono or stereo; the board always leaves in stereo, as it does in the app.
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
           && out == juce::AudioChannelSet::stereo();
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // The host tells this processor the rate and block; the board's own processor has to be told too, because it
    // reads getSampleRate() for the MIDI clock. A host normally does this through the wrapper's player, which the
    // plugin does not have.
    setRateAndBufferSizeDetails (sampleRate, maximumExpectedSamplesPerBlock);
    session.prepare (sampleRate, maximumExpectedSamplesPerBlock);

    // Card 8.6: nothing is playing yet, so the chain's latency can be reported at once rather than waiting for a gap.
    reportedLatency = chainLatencySamples();
    setLatencySamples (reportedLatency);

    // Card 8.2: the board is edited from the message thread and snapshots are retired at N+2 blocks, so something
    // has to run collectGarbage() even when no editor is open. Most hosts prepare on their message thread; one that
    // prepares on a worker thread gets the timer started for it, on the right thread, through a weak reference so a
    // processor destroyed in the meantime is not touched (review, PR #2).
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        startTimerHz (30);
    }
    else
    {
        juce::WeakReference<PluginProcessor> safeThis (this);

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->startTimerHz (30);
        });
    }
}

void PluginProcessor::releaseResources()
{
    stopTimer();
    session.getProcessor().releaseResources();

    // The timer was what would have landed a restore that is still waiting out its fade, and there is no audio left to
    // fade: land it now, or the board stays bypassed and the project's board is never applied until some later
    // prepareToPlay.
    landPendingState();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Card 8.6: the host's tempo goes in before the board runs, so a tempo-aware pedal sees it on this block rather
    // than the next one.
    updateHostTempo();

    session.getProcessor().processBlock (buffer, midi);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    // Card 8.5: the same board the app window draws, with the plugin's answers behind the HostServices seam. What
    // this returns belongs to the host's wrapper, which deletes it when the editor window closes, so a fresh one each
    // time is what it expects -- never a cached instance.
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    // A restore that is waiting behind its fade has been accepted already, so it is the state: a host that saves right
    // after loading (an undo point, a preset store) must get it back, not the board it is about to replace.
    juce::var state;

    {
        const juce::ScopedLock lock (pendingLock);
        state = pendingState;
    }

    if (state.isVoid())
        state = session.captureState();

    const auto json = juce::JSON::toString (state, false);
    destination.replaceAll (json.toRawUTF8(), (size_t) json.getNumBytesAsUTF8());
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    juce::var state;

    // Anything that is not JSON is not our business, and is dropped without touching the board.
    if (juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, sizeInBytes), state).failed())
        return;

    beginStateRestore (state);
}

void PluginProcessor::beginStateRestore (const juce::var& state)
{
    // A state that will not read is refused here, before anything is bypassed for it: the fade below would otherwise
    // dip the board for a restore that then applies nothing.
    if (! session.canRestoreState (state))
        return;

    // No audio is running (a host usually restores before prepareToPlay), so there is nothing to click and the swap
    // can land at once. A restore still waiting from earlier is older than this one and is dropped, not landed on top
    // of it afterwards.
    if (! isTimerRunning())
    {
        bool unbypass = false;

        {
            const juce::ScopedLock lock (pendingLock);
            unbypass = ! pendingState.isVoid() && pendingStateUnbypass;
            pendingState = juce::var();
        }

        if (session.restoreState (state))
            afterBoardRestored();

        if (unbypass)
            setBoardBypassedQuietly (false);

        return;
    }

    // Otherwise the board's own bypass smoother is the fade: bypass, swap, unbypass. A second restore while one is
    // waiting replaces the first, as two quick program changes do in the app — but it must not take the bypass state
    // over: the board was bypassed for the first restore, so reading it again would record "leave it bypassed" and
    // the board would never come back (review, PR #2).
    {
        // A host may restore from a thread of its own while the timer reads these on the message thread.
        const juce::ScopedLock lock (pendingLock);

        const bool alreadyWaiting = ! pendingState.isVoid();
        pendingState = state;
        pendingStateDueMs = juce::Time::currentTimeMillis() + kStateFadeMs;

        if (! alreadyWaiting)
            pendingStateUnbypass = ! session.isBoardBypassed();
    }

    setBoardBypassedQuietly (true);
}

void PluginProcessor::landPendingState()
{
    juce::var state;
    bool unbypass = false;

    {
        const juce::ScopedLock lock (pendingLock);

        if (pendingState.isVoid())
            return;

        state = std::move (pendingState);
        pendingState = juce::var();
        unbypass = pendingStateUnbypass;
    }

    if (session.restoreState (state))
        afterBoardRestored();

    if (unbypass)
        setBoardBypassedQuietly (false);
}

void PluginProcessor::setBoardBypassedQuietly (bool bypassed)
{
    // The fade around a restore is this plugin's own doing. Left to the poll, the host would be told "Board Bypass
    // moved" twice for every restore, and a host in a write mode would record both as automation.
    session.setBoardBypassed (bypassed);

    if (auto* parameter = parameters.find ("board/bypass"))
        parameter->resync();
}

void PluginProcessor::timerCallback()
{
    session.collectGarbage();

    // A macro moved by the host's automation or by a mapped controller writes its targets here, and nowhere else:
    // MacroBank::update() is only ever called from a timer, and the editor's is the only other one. Without this a
    // host lane on macro/N did nothing whenever the window was closed, which is when automation is played.
    session.getMacros().update();

    // Card 8.4: the slot switches follow the chain, and a value that moved in the board is announced to the host
    // exactly once. Both are message-thread work, which is what hosts require for names and gestures.
    if (parameters.refreshChain())
        updateHostDisplay();

    parameters.poll();

    // Card 8.6: a latency change is held until the input has been silent, the same rule TUBE's oversampling switch
    // uses (card 5.33). A host's delay compensation jumping under a held note is audible; in the gap between phrases
    // it is not. `inPeak` is what arrived this block, before any trim.
    const int latency = chainLatencySamples();

    if (latency != reportedLatency && session.getProcessor().inputPeak() < 1.0e-5f)
    {
        reportedLatency = latency;
        setLatencySamples (latency);
    }

    {
        const juce::ScopedLock lock (pendingLock);

        if (pendingState.isVoid() || juce::Time::currentTimeMillis() < pendingStateDueMs)
            return;
    }

    landPendingState();
}

void PluginProcessor::afterBoardRestored()
{
    // The chain underneath the automation moved, so slot/N/on now drive different pedals; and every parameter's value
    // came from the host's own state, so it must not be echoed back to it as a change (cards 8.3, 8.4).
    //
    // refreshChain() records the chain's new signature as it goes, so the timer's next tick would answer "nothing
    // changed" and a host's generic editor would keep showing the old chain after a restore. Ask for the redraw here
    // instead (review, PR #2).
    if (parameters.refreshChain())
        notifyHostOfChainChange();

    parameters.resync();
}

void PluginProcessor::notifyHostOfChainChange()
{
    // Hosts expect this on the message thread. A restore before prepareToPlay is already on it; one that arrives on a
    // host's own worker thread is handed over rather than called from there.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        updateHostDisplay();
        return;
    }

    juce::WeakReference<PluginProcessor> safeThis (this);

    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->updateHostDisplay();
    });
}

bool PluginProcessor::updateHostTempo()
{
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                // Only when it moved: set() bumps the registry's change counter, which the editor reads as "the
                // board was edited". Written on every block it kept a freshly loaded preset starred.
                if (tempoIndex != ParameterRegistry::kInvalid && std::isfinite (*bpm))
                {
                    const auto hostTempo = (float) juce::jlimit (20.0, 300.0, *bpm);

                    if (std::abs (session.getRegistry().get (tempoIndex) - hostTempo) > 1.0e-4f)
                        session.getRegistry().set (tempoIndex, hostTempo);
                }

                // The host owns the board's tempo while it reports one. A MIDI clock on the same block keeps
                // measuring, but must not overwrite what the host just said (card 8.6).
                clock::setHostTempoOwns (true);
                return true;
            }
        }
    }

    clock::setHostTempoOwns (false);
    return false;
}

int PluginProcessor::chainLatencySamples()
{
    int total = 0;
    auto& model = session.getModel();

    // The same sum the board's own dry ring compensates and the app's status line shows. A bypassed board still
    // delays by this much: its dry copy is read from the ring this far back.
    for (int i = 0; i < model.size(); ++i)
        if (auto* slot = model.at (i))
            if (auto* effect = slot->getEffect())
                total += juce::jmax (0, effect->latencySamples());

    return total;
}
} // namespace pb
