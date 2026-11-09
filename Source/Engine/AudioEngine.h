#pragma once
// Card 0.2: owns the audio device and the player that drives BoardProcessor.
// The card asks for 2 in / 2 out at 48 kHz and 64 samples, remembered across launches.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace pb
{
class AudioEngine
{
public:
    explicit AudioEngine (juce::AudioProcessor& processorToPlay);
    ~AudioEngine();

    // Opens the device, walking a fallback chain (see the .cpp). Returns false only when nothing opened.
    bool initialise();
    void shutdown();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::AudioIODevice* getCurrentDevice() const noexcept { return deviceManager.getCurrentAudioDevice(); }

    // Why the last attempt failed, for the status line; empty on success.
    juce::String getLastError() const { return lastError; }

    // True when the saved setup could not be honoured (buffer size or channel count had to give).
    bool isUsingFallback() const noexcept { return usedFallback; }

    void saveSetup() const;

    // ~/Library/Application Support/pump BOARD/audio.xml
    static juce::File getSetupFile();

    // What the device reports: input + output + one buffer, in samples and ms.
    int getReportedLatencySamples() const;
    double getReportedLatencyMs() const;

private:
    bool open (int numInputs, int numOutputs, int bufferSize, const juce::XmlElement* saved, bool persist = true);
    bool restoreSaved (const juce::XmlElement* saved);

    juce::AudioProcessor& processor;
    juce::AudioProcessorPlayer player;
    juce::AudioDeviceManager deviceManager;
    juce::String lastError;
    bool usedFallback = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace pb
