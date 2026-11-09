#include "Engine/AudioEngine.h"

namespace pb
{
namespace
{
constexpr double kPreferredSampleRate = 48000.0;
constexpr int kPreferredBufferSize = 64;
constexpr int kFallbackBufferSize = 128;

std::unique_ptr<juce::XmlElement> readSetupFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName ("PUMPSETUP"))
        return {};

    return xml;
}
} // namespace

AudioEngine::AudioEngine (juce::AudioProcessor& processorToPlay)
    : processor (processorToPlay)
{
    // The player is given the processor before a device exists; audioDeviceAboutToStart() then re-prepares it
    // with the real rate and block size once one opens.
    player.setProcessor (&processor);
    deviceManager.addAudioCallback (&player);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::open (int numInputs, int numOutputs, int bufferSize, const juce::XmlElement* saved, bool persist)
{
    deviceManager.closeAudioDevice();

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.sampleRate = kPreferredSampleRate;
    setup.bufferSize = bufferSize;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;

    lastError = deviceManager.initialise (numInputs, numOutputs, saved, true, {}, &setup);

    auto* device = deviceManager.getCurrentAudioDevice();

    if (lastError.isNotEmpty() || device == nullptr)
    {
        if (lastError.isEmpty())
            lastError = "no audio device could be opened";

        deviceManager.closeAudioDevice();
        return false;
    }

    if (device->getActiveOutputChannels().countNumberOfSetBits() < 2)
    {
        lastError = "the device offered fewer than two outputs";
        deviceManager.closeAudioDevice();
        return false;
    }

    lastError.clear();

    if (persist)
        saveSetup();

    return true;
}

bool AudioEngine::restoreSaved (const juce::XmlElement* saved)
{
    // Open something first: the manager needs a live device before it can be pointed at another one. This stand-in
    // is not written to audio.xml: it used to be, which replaced the saved setup with the default device before it
    // was even tried, so the interface was forgotten after one launch (and for good if it was unplugged that day).
    if (! open (2, 2, kPreferredBufferSize, nullptr, false))
        return false;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.outputDeviceName = saved->getStringAttribute ("output");
    setup.inputDeviceName = saved->getStringAttribute ("input");
    setup.sampleRate = saved->getDoubleAttribute ("rate", kPreferredSampleRate);
    setup.bufferSize = saved->getIntAttribute ("buffer", kPreferredBufferSize);

    const int numInputs = juce::jlimit (0, 2, saved->getIntAttribute ("inputs", 2));
    const int numOutputs = juce::jlimit (0, 2, saved->getIntAttribute ("outputs", 2));

    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, numInputs, true);
    setup.outputChannels.setRange (0, numOutputs, true);

    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
    {
        lastError.clear();
        return true;
    }

    // The saved interface is gone (unplugged, renamed). Keep playing through what open() gave us.
    lastError = error;
    usedFallback = true;

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return open (2, 2, kFallbackBufferSize, nullptr, false);

    return true;
}

bool AudioEngine::initialise()
{
    usedFallback = false;
    lastError.clear();

    // JUCE 9's AudioDeviceManager::createStateXml() returns nothing on the CoreAudio backend, so the setup
    // is written and read by us (see saveSetup) rather than with JUCE's state XML. Same file, same job.
    const auto saved = readSetupFile (getSetupFile());

    // 1. the interface and buffer size this machine used last time
    if (saved != nullptr && restoreSaved (saved.get()))
        return true;

    // 2. a fresh stereo device at the plan's target: 48 kHz, 64 samples
    if (open (2, 2, kPreferredBufferSize, nullptr))
        return true;

    // 3. the same, 128 samples: plenty of interfaces refuse 64
    if (open (2, 2, kFallbackBufferSize, nullptr))
    {
        usedFallback = true;
        return true;
    }

    // 4. no usable input — a laptop's built-in output still opens, which keeps the app usable without an interface
    if (open (0, 2, kFallbackBufferSize, nullptr))
    {
        usedFallback = true;
        return true;
    }

    if (lastError.isEmpty())
        lastError = "no audio device could be opened";

    return false;
}

void AudioEngine::shutdown()
{
    // Nothing else writes audio.xml after launch, so a device picked in Settings is remembered from here. Not after a
    // fallback launch: the setup in use then is the stand-in, and the file still holds the interface that was missing.
    if (! usedFallback)
        saveSetup();

    deviceManager.removeAudioCallback (&player);
    deviceManager.closeAudioDevice();
    player.setProcessor (nullptr);
}

void AudioEngine::saveSetup() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    const auto file = getSetupFile();
    file.getParentDirectory().createDirectory();

    const auto setup = deviceManager.getAudioDeviceSetup();

    juce::XmlElement xml ("PUMPSETUP");
    xml.setAttribute ("type", device->getTypeName());
    xml.setAttribute ("output", setup.outputDeviceName);
    xml.setAttribute ("input", setup.inputDeviceName);
    xml.setAttribute ("rate", device->getCurrentSampleRate());
    xml.setAttribute ("buffer", device->getCurrentBufferSizeSamples());
    xml.setAttribute ("inputs", device->getActiveInputChannels().countNumberOfSetBits());
    xml.setAttribute ("outputs", device->getActiveOutputChannels().countNumberOfSetBits());

    xml.writeTo (file);
}

juce::File AudioEngine::getSetupFile()
{
    // Built by hand rather than via File::userApplicationDataDirectory, whose meaning differs per platform;
    // the card names ~/Library/Application Support/pump BOARD/audio.xml exactly.
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile ("Library")
             .getChildFile ("Application Support")
             .getChildFile ("pump BOARD")
             .getChildFile ("audio.xml");
}

int AudioEngine::getReportedLatencySamples() const
{
    if (auto* device = getCurrentDevice())
        return device->getInputLatencyInSamples()
             + device->getOutputLatencyInSamples()
             + device->getCurrentBufferSizeSamples();

    return 0;
}

double AudioEngine::getReportedLatencyMs() const
{
    if (auto* device = getCurrentDevice())
        if (const double rate = device->getCurrentSampleRate(); rate > 0.0)
            return 1000.0 * (double) getReportedLatencySamples() / rate;

    return 0.0;
}
} // namespace pb
