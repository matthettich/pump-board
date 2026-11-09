#include "Plugin/PluginEditor.h"
#include "Plugin/PluginProcessor.h"
#include "UI/PumpStyle.h"

namespace pb
{
namespace
{
// Card 8.5: the plugin's answers to the questions BoardContent asks. A host has no device to report and no working
// copy to keep, so most of this is simply absent — which is the point of asking instead of assuming an AudioEngine.
class PluginHost final : public HostServices
{
public:
    explicit PluginHost (PluginProcessor& processorIn) : processor (processorIn) {}

    Summary getSummary() const override
    {
        Summary summary;

        summary.hasAudio = processor.getSampleRate() > 0.0;
        summary.sampleRateHz = processor.getSampleRate();
        summary.blockSize = juce::jmax (1, processor.getBlockSize());

        // No round trip and no fallback: those are the host's and the device's, not this plugin's. The board's own
        // share of the delay is printed from the chain beside the tempo instead.
        return summary;
    }

    std::unique_ptr<juce::Component> createSettingsView (std::function<void()>) override
    {
        return nullptr;             // a host owns the device; there is nothing here to pick
    }

    bool hasSettingsView() const override { return false; }
    juce::File workingBoardFile() const override { return {}; }   // the host's state is the board, not a file
    bool tempoEditable() const override { return false; }         // card 8.6: the host's tempo is the tempo
    bool preparesBoardOnDeviceChange() const override { return false; }

private:
    PluginProcessor& processor;
};
} // namespace

PluginEditor::PluginEditor (PluginProcessor& processorIn)
    : juce::AudioProcessorEditor (&processorIn),
      processor (processorIn)
{
    setOpaque (true);

    auto& session = processor.getSession();

    host = std::make_unique<PluginHost> (processor);
    content = std::make_unique<BoardContent> (*host, session.getProcessor(), session.getRegistry(),
                                              session.getModel(), session.getMidiRouter(),
                                              session.getKeyRouter(), session.getLearn(), session.getMacros());

    addAndMakeVisible (*content);

    setSize (1300, 720);
    setResizable (true, false);
    setResizeLimits (1300, 700, 4096, 3072);   // the app window's limits: the board's layout is fixed

    // Card 8.5: with no working copy, a host restore is the one thing that replaces the chain while the panel is
    // looking at it. The session says so, and the content re-points itself at the new slots before the panel's own
    // timer can touch a freed one.
    session.setEditorAttached (true);
    // A host may restore its state from a thread of its own, and re-pointing the panel is message-thread work: hand
    // it over rather than touching components from wherever the restore arrived (review, PR #2).
    session.onBoardReplaced = [safe = juce::Component::SafePointer<PluginEditor> (this)]
    {
        const auto repoint = [safe]
        {
            if (safe != nullptr && safe->content != nullptr)
                safe->content->repointSlot();
        };

        if (juce::MessageManager::existsAndIsCurrentThread())
            repoint();
        else
            juce::MessageManager::callAsync (repoint);
    };
}

PluginEditor::~PluginEditor()
{
    auto& session = processor.getSession();
    session.onBoardReplaced = nullptr;
    session.setEditorAttached (false);

    content.reset();        // before `host`, which the content holds a reference to
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (style::idm::paper());
}

void PluginEditor::resized()
{
    if (content != nullptr)
        content->setBounds (getLocalBounds());
}

void PluginEditor::parentHierarchyChanged()
{
    // Mappings from the keyboard only work while the editor has focus; a host that eats a key wins, and that is
    // documented rather than fought (card 8.5).
    if (isShowing() && content != nullptr)
        content->grabKeyboardFocus();
}
} // namespace pb
