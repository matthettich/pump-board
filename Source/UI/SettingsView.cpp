#include "UI/SettingsView.h"
#include "Engine/AudioEngine.h"

namespace pb
{
using namespace style;

namespace
{
// The probe's way onto the device. It runs after the player's callback, so the block already holds the chain's
// output and the click is added on top of it; the raw input is what a loopback cable brings back.
class ProbeCallback final : public juce::AudioIODeviceCallback
{
public:
    explicit ProbeCallback (LatencyProbe& probeToDrive) : probe (probeToDrive) {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        probe.process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

private:
    LatencyProbe& probe;
};
} // namespace

SettingsView::SettingsView (AudioEngine& engineIn)
    : engine (engineIn),
      // 0..2 inputs so a laptop with no input can still be configured; 2 outputs exactly; MIDI inputs listed.
      selector (engineIn.getDeviceManager(), 0, 2, 2, 2, true, false, true, false)
{
    theme.build (accents::machine(), style::isDark());

    addAndMakeVisible (selector);
    addAndMakeVisible (done);
    addAndMakeVisible (measure);

    done.setColour (juce::TextButton::buttonColourId, theme.panel);
    done.setColour (juce::TextButton::textColourOffId, theme.ink);
    done.onClick = [this]
    {
        if (onClose != nullptr)
            onClose();
    };

    measure.setColour (juce::TextButton::buttonColourId, theme.panel);
    measure.setColour (juce::TextButton::textColourOffId, theme.ink);
    measure.setTooltip ("Patch an output back into an input with a cable, then press this: the app sends one "
                        "click and times how long it takes to come back.");
    measure.onClick = [this] { startMeasurement(); };

    // Card 5.5: the light skin. Flipping the flag is not enough — every panel builds its palette in its
    // constructor — so the window rebuilds its content on the next message, never from inside this click.
    addAndMakeVisible (skin);
    skin.setColour (juce::TextButton::buttonColourId, theme.panel);
    skin.setColour (juce::TextButton::textColourOffId, theme.ink);
    skin.setButtonText (style::isDark() ? "SKIN: DARK" : "SKIN: LIGHT");
    skin.setTooltip ("Switch the whole app between the dark skin and the light one.");
    skin.onClick = [this]
    {
        style::setDark (! style::isDark());
        skin.setButtonText (style::isDark() ? "SKIN: DARK" : "SKIN: LIGHT");

        if (onSkinChanged != nullptr)
        {
            const auto callback = onSkinChanged;
            juce::MessageManager::callAsync ([callback] { callback(); });
        }
    };

    startTimerHz (4);
}

SettingsView::~SettingsView()
{
    stopTimer();
    stopMeasurement (false);           // the device must stop calling us before the probe goes away
}

void SettingsView::startMeasurement()
{
    if (measuring)
    {
        stopMeasurement (false);
        measureStatus = "MEASUREMENT CANCELLED";
        repaint();
        return;
    }

    auto* device = engine.getCurrentDevice();

    if (device == nullptr)
    {
        measureStatus = "NO DEVICE OPEN TO MEASURE";
        repaint();
        return;
    }

    // Every buffer the audio thread touches is allocated here, before a block can see it.
    probe.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());

    if (probeCallback == nullptr)
        probeCallback = std::make_unique<ProbeCallback> (probe);

    engine.getDeviceManager().addAudioCallback (probeCallback.get());
    probe.begin();

    measuring = true;
    measureStatus = "SENDING A CLICK...";
    repaint();
}

void SettingsView::stopMeasurement (bool keepResult)
{
    if (probeCallback != nullptr)
        engine.getDeviceManager().removeAudioCallback (probeCallback.get());

    probe.cancel();
    measuring = false;

    if (! keepResult)
        measureStatus = {};
}

void SettingsView::timerCallback()
{
    if (measuring && probe.isFinished())
    {
        const float measured = probe.finish();
        stopMeasurement (true);

        // -1 is "the window closed with nothing in it", which is a cable that is not patched, not a 0 ms delay.
        measureStatus = measured >= 0.0f
                          ? "MEASURED " + juce::String (measured, 2) + " ms ROUND TRIP"
                          : juce::String ("NOTHING CAME BACK - CHECK THE LOOPBACK CABLE");
    }

    repaint();
}

void SettingsView::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);

    const auto header = getLocalBounds().removeFromTop (56).reduced (16, 10);

    draw::text (g, "AUDIO", { (float) header.getX(), (float) header.getY(), 200.0f, 20.0f },
                theme.ink, 15.0f, juce::Justification::centredLeft, true, 1.4f);

    juce::String status;
    if (auto* device = engine.getCurrentDevice())
    {
        status = juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz   "
               + juce::String (device->getCurrentBufferSizeSamples()) + " samples   "
               + juce::String (engine.getReportedLatencyMs(), 1) + " ms reported round trip";
    }
    else
    {
        status = "no device open" + juce::String (engine.getLastError().isNotEmpty() ? "  -  " : "")
               + engine.getLastError();
    }

    draw::text (g, status, { (float) header.getX(), (float) header.getY() + 22.0f, 620.0f, 18.0f },
                theme.ink2, 11.0f, juce::Justification::centredLeft, false);

    // Card 0.4: the measured number, or what to do to get one.
    draw::text (g, measureStatus.isNotEmpty()
                     ? measureStatus
                     : juce::String ("MEASURING NEEDS AN OUTPUT PATCHED BACK INTO AN INPUT"),
                { (float) header.getX(), (float) header.getY() + 44.0f, 720.0f, 18.0f },
                measureStatus.isNotEmpty() ? theme.neon : theme.dim,
                10.5f, juce::Justification::centredLeft, true, 1.2f);
}

void SettingsView::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (48);

    auto footer = r.removeFromBottom (44);
    done.setBounds (footer.removeFromRight (96).reduced (0, 6));
    measure.setBounds (footer.removeFromRight (196).reduced (0, 6));
    skin.setBounds (footer.removeFromRight (124).reduced (0, 6));

    selector.setBounds (r);
}
} // namespace pb
