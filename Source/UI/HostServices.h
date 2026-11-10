#pragma once
// Card 8.5: everything the board's content needs to know about the thing playing the audio, asked as questions
// rather than as an AudioEngine. The standalone answers with its device; the plugin editor answers with the host.
// That difference is the whole reason the content could not stay in MainWindow.cpp, which is why this interface is
// small: anything both hosts share stays inside BoardContent.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace pb
{
class HostServices
{
public:
    virtual ~HostServices() = default;

    // The facts the status line prints. `roundTripKnown` is false where there is no device round trip to report,
    // which is the plugin: a host's own latency is not ours to claim, and the board's share is printed separately.
    struct Summary
    {
        bool hasAudio = false;
        double sampleRateHz = 0.0;
        int blockSize = 0;
        double roundTripMs = 0.0;
        bool roundTripKnown = false;
        bool fallback = false;
        juce::String error;
    };

    virtual Summary getSummary() const = 0;

    // The device picker and its round-trip measurement, or nullptr where the host owns the device (a plugin). The
    // overlay asks to be hidden through `closeRequested`, because its own DONE button is not the content's child and
    // the content does not know its type.
    virtual std::unique_ptr<juce::Component> createSettingsView (std::function<void()> closeRequested) = 0;

    // False where the SETTINGS button has nothing behind it, so it is hidden rather than offered and inert.
    virtual bool hasSettingsView() const { return true; }

    // The working copy that reopens next launch. An invalid File means the host keeps the board itself: a plugin
    // instance's board lives in the host's state and nowhere else (card 8.3), so it neither reads nor writes one.
    virtual juce::File workingBoardFile() const = 0;

    // Card 8.5: the plugin shows the host's tempo and cannot edit it. The standalone's tempo comes from its field, a
    // MIDI clock and the saved setting (cards 5.25, 8.6).
    virtual bool tempoEditable() const { return true; }

    // The standalone re-prepares the board itself when the device changes. A plugin's host calls prepareToPlay,
    // which the processor already answers, so the content must not prepare a second time behind its back.
    virtual bool preparesBoardOnDeviceChange() const { return true; }
};
} // namespace pb
