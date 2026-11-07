#include "Plugin/PluginProcessor.h"

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
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    session.loadDefaultBoard();
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

    // Card 8.2: the board is edited from the message thread and snapshots are retired at N+2 blocks, so something
    // has to run collectGarbage() even when no editor is open. A host prepares on its message thread; if one did
    // not, the timer is simply not started rather than started from the wrong thread.
    if (juce::MessageManager::existsAndIsCurrentThread())
        startTimerHz (30);
}

void PluginProcessor::releaseResources()
{
    stopTimer();
    session.getProcessor().releaseResources();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
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
        session.restoreState (state);
        return;
    }

    // Otherwise the board's own bypass smoother is the fade: bypass, swap, unbypass. A second restore while one is
    // waiting replaces it, as two quick program changes do in the app.
    pendingState = state;
    pendingStateDueMs = juce::Time::currentTimeMillis() + kStateFadeMs;
    pendingStateUnbypass = ! session.isBoardBypassed();
    session.setBoardBypassed (true);
}

void PluginProcessor::timerCallback()
{
    session.collectGarbage();

    if (pendingState.isVoid() || juce::Time::currentTimeMillis() < pendingStateDueMs)
        return;

    auto state = std::move (pendingState);
    pendingState = juce::var();
    const bool unbypass = pendingStateUnbypass;

    session.restoreState (state);

    if (unbypass)
        session.setBoardBypassed (false);
}
} // namespace pb
