#include "Plugin/PluginProcessor.h"

namespace pb
{
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
} // namespace pb
