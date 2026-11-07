#pragma once
// Card 8.5: the plugin's window. It is BoardContent plus a host-services adapter, and nothing else: the board, the
// macro strip, the preset menu and the learn flow are the app's own components, so there is one board UI to fix
// rather than two that drift apart.
#include "UI/BoardContent.h"
#include "UI/HostServices.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace pb
{
class PluginProcessor;

class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    PluginProcessor& processor;      // the session behind the content; the editor owns none of it
    std::unique_ptr<HostServices> host;
    std::unique_ptr<BoardContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace pb
