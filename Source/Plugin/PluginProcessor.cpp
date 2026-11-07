#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Control/MidiClock.h"

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
    // processor destroyed in the meantime is not touched (CodeRabbit, PR #2).
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
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Card 8.6: the host's tempo goes in before the board runs, so a tempo-aware pedal sees it on this block rather
    // than the next one.
    updateHostTempo();

    session.getProcessor().processBlock (buffer, midi);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    const auto json = juce::JSON::toString (session.captureState(), false);
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
    // No audio is running (a host usually restores before prepareToPlay), so there is nothing to click and the swap
    // can land at once.
    if (! isTimerRunning())
    {
        if (session.restoreState (state))
            afterBoardRestored();

        return;
    }

    // Otherwise the board's own bypass smoother is the fade: bypass, swap, unbypass. A second restore while one is
    // waiting replaces the first, as two quick program changes do in the app — but it must not take the bypass state
    // over: the board was bypassed for the first restore, so reading it again would record "leave it bypassed" and
    // the board would never come back (CodeRabbit, PR #2).
    const bool alreadyWaiting = ! pendingState.isVoid();
    pendingState = state;
    pendingStateDueMs = juce::Time::currentTimeMillis() + kStateFadeMs;

    if (! alreadyWaiting)
        pendingStateUnbypass = ! session.isBoardBypassed();

    session.setBoardBypassed (true);
}

void PluginProcessor::timerCallback()
{
    session.collectGarbage();

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

    if (pendingState.isVoid() || juce::Time::currentTimeMillis() < pendingStateDueMs)
        return;

    auto state = std::move (pendingState);
    pendingState = juce::var();
    const bool unbypass = pendingStateUnbypass;

    if (session.restoreState (state))
        afterBoardRestored();

    if (unbypass)
        session.setBoardBypassed (false);
}

void PluginProcessor::afterBoardRestored()
{
    // The chain underneath the automation moved, so slot/N/on now drive different pedals; and every parameter's value
    // came from the host's own state, so it must not be echoed back to it as a change (cards 8.3, 8.4).
    parameters.refreshChain();
    parameters.resync();
}

bool PluginProcessor::updateHostTempo()
{
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                if (tempoIndex != ParameterRegistry::kInvalid)
                    session.getRegistry().set (tempoIndex, (float) juce::jlimit (20.0, 300.0, *bpm));

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
