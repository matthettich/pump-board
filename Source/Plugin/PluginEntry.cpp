#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
// Card 8.1: a pass-through processor, so the target, both bundles and the validators have something real to work
// on before any of the board is involved. Card 8.2 replaces it with the board itself.
class PumpBoardStub final : public juce::AudioProcessor
{
public:
    PumpBoardStub()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return JucePlugin_Name; }

    // A guitar arrives mono or stereo; the board always leaves in stereo, so a mono track is accepted as it is
    // rather than forcing the host to upmix before the input trim.
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto in  = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();

        return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
               && out == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();

        if (numSamples <= 0)
            return;

        // Mono in: copy it up, exactly as BoardProcessor does for a guitar wired to input 1.
        if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() >= 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};
} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PumpBoardStub();
}
