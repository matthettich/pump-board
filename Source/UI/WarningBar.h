#pragma once
// Card 4.3: what a load had to drop or repair, in a bar that can be dismissed. Never a modal dialog: a
// warning about a preset must not stop a musician from playing.
#include "UI/PumpStyle.h"

namespace pb
{
class WarningBar final : public juce::Component
{
public:
    WarningBar();

    void show (const juce::StringArray& messages, const juce::String& title);

    // The window hides the bar; the bar never hides itself, so the window keeps one place that decides.
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kMaxLines = 3;

    juce::TextButton closeButton { "DISMISS" };
    juce::StringArray lines;
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarningBar)
};
} // namespace pb
