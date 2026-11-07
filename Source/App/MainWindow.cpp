#include "App/MainWindow.h"
#include "UI/BoardContent.h"
#include "UI/PumpStyle.h"
#include "UI/SettingsView.h"
#include <functional>

namespace pb
{
namespace
{
// Card 8.5: the standalone's answers to the questions BoardContent asks. Everything here used to be a direct
// AudioEngine& inside the content; keeping the device behind this interface is what lets the plugin editor use the
// same window content without pretending to have a device it does not have.
class StandaloneHost final : public HostServices
{
public:
    explicit StandaloneHost (AudioEngine& engineIn) : engine (engineIn) {}

    Summary getSummary() const override
    {
        Summary summary;

        if (auto* device = engine.getCurrentDevice())
        {
            summary.hasAudio = true;
            summary.sampleRateHz = device->getCurrentSampleRate();
            summary.blockSize = device->getCurrentBufferSizeSamples();
            summary.roundTripMs = engine.getReportedLatencyMs();
            summary.roundTripKnown = true;
            summary.fallback = engine.isUsingFallback();
        }
        else
        {
            summary.error = engine.getLastError();
        }

        return summary;
    }

    std::unique_ptr<juce::Component> createSettingsView (std::function<void()> closeRequested) override
    {
        auto view = std::make_unique<SettingsView> (engine);
        view->onClose = std::move (closeRequested);
        return view;
    }

    juce::File workingBoardFile() const override { return pb::workingBoardFile(); }

private:
    AudioEngine& engine;
};
} // namespace

MainWindow::MainWindow (AudioEngine& engineIn, BoardProcessor& processorIn, ParameterRegistry& registryIn,
                        BoardModel& modelIn, MidiRouter& routerIn)
    : juce::DocumentWindow ("pump BOARD", style::idm::paper(), juce::DocumentWindow::allButtons),
      engine (engineIn),
      processor (processorIn),
      registry (registryIn),
      model (modelIn),
      router (routerIn),
      keys (routerIn),
      learn (registryIn, routerIn, keys),
      macros (registryIn),
      host (std::make_unique<StandaloneHost> (engineIn))
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setResizeLimits (1300, 700, 4096, 3072);   // the board's fixed layout: 16 slots in two rows of 8, plus the side column
    buildContent();
    centreWithSize (1300, 720);
    setVisible (true);

    if (auto* content = getContentComponent())
        content->grabKeyboardFocus();     // so a key press reaches BoardContent::keyPressed
}

void MainWindow::buildContent()
{
    auto* content = new BoardContent (*host, processor, registry, model, router, keys, learn, macros);

    setContentOwned (content, false);

    // Only when the window is already on screen: grabbing focus before that trips JUCE's own assertion. At
    // startup the constructor does it once the window is visible.
    if (isShowing())
        if (auto* c = getContentComponent())
            c->grabKeyboardFocus();
}

MainWindow::~MainWindow()
{
    // The window's content (and its timer) must go before the device that feeds it.
    clearContentComponent();
    engine.shutdown();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
} // namespace pb
