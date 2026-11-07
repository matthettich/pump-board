#pragma once
// Card 0.2: the device picker. Wraps JUCE's selector and shows what the device reports.
#include "Engine/LatencyProbe.h"
#include "UI/PumpStyle.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

namespace pb
{
class AudioEngine;

class SettingsView final : public juce::Component, private juce::Timer
{
public:
    explicit SettingsView (AudioEngine&);
    ~SettingsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

private:
    void timerCallback() override;

    // Card 0.4: the click goes out and the peak comes back. The callback is on the device only while measuring,
    // so a normal session pays nothing for it.
    void startMeasurement();
    void stopMeasurement (bool keepResult);

    AudioEngine& engine;
    style::Theme theme;
    juce::AudioDeviceSelectorComponent selector;
    juce::TextButton done { "DONE" };
    juce::TextButton measure { "MEASURE ROUND TRIP" };
    LatencyProbe probe;
    std::unique_ptr<juce::AudioIODeviceCallback> probeCallback;
    juce::String measureStatus;
    bool measuring = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsView)
};
} // namespace pb
