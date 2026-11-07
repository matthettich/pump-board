#include <juce_gui_extra/juce_gui_extra.h>
#include "App/MainWindow.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/AudioEngine.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Engine/Slot.h"
#include "UI/BoardPrototype.h"
#include "UI/MacroStrip.h"
#include "UI/GenericEffectEditor.h"
#include "UI/IdmLookAndFeel.h"
#include "UI/SlotView.h"
#include "UI/TempoField.h"

class PumpBoardApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "pump BOARD"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        // Every stock widget (menus, alert windows, the device selector) in the IDM pink & green skin.
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        // Offscreen render, so the look can be inspected without a window or a device:
        //   "pump BOARD.app/Contents/MacOS/pump BOARD" --render board.png [scale]
        const auto args = getCommandLineParameterArray();

        if (args.size() >= 2 && args[0] == "--render")
        {
            pb::BoardPrototype board;
            board.setSize (1100, 700);
            board.setPedalCount (7);      // the painted mockup shows the seven lit pedals of the design
            board.setStatusText ("48 kHz    64 smp    ROUND TRIP 4.6 ms    LOAD 12%");
            board.renderToPng (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]),
                               args.size() >= 3 ? args[2].getFloatValue() : 2.0f);
            quit();
            return;
        }

        // The board with a real editor panel on it, for judging card 1.5 without launching the app:
        //   "pump BOARD.app/Contents/MacOS/pump BOARD" --render-editor editor.png [scale]
        if (args.size() >= 2 && args[0] == "--render-editor")
        {
            pb::registerBuiltInEffects (pb::EffectRegistry::instance());

            const auto typeId = args.size() >= 4 ? args[3] : juce::String ("pump.spring");

            pb::ParameterRegistry previewRegistry;
            pb::Slot slot ("preview", previewRegistry);
            slot.setEffect (pb::EffectRegistry::instance().create (typeId));
            slot.prepare (48000.0, 64);

            pb::BoardPrototype board;
            board.setSize (1100, 700);
            board.setPaintedEditorVisible (false);
            board.setPaintedSlotsVisible (false);     // one real pedal on a real board, not the mockup

            auto* previewEffect = slot.getEffect();
            juce::String lines[2];

            if (previewEffect != nullptr)
            {
                // Same rule as the window: the first two real params, so the pedal cannot lie about itself.
                const auto params = previewEffect->params();
                const auto& indices = slot.paramIndices();
                int shown = 0;

                for (size_t p = 0; p < params.size() && shown < 2; ++p)
                {
                    if (params[p].isToggle || params[p].id == "model")
                        continue;

                    const int index = p < indices.size() ? indices[p] : pb::ParameterRegistry::kInvalid;
                    const float value = index != pb::ParameterRegistry::kInvalid ? previewRegistry.get (index)
                                                                                : params[p].defaultValue;
                    lines[shown++] = params[p].name.toUpperCase() + " " + pb::formatParamValue (params[p], value);
                }
            }

            juce::String category ("UTIL");          // the pedal's real category, as the running board shows it

            for (const auto& descriptor : pb::EffectRegistry::instance().list())
                if (descriptor.typeId == typeId)
                    category = descriptor.category.toUpperCase();
            board.setSlotState (0, previewEffect != nullptr ? previewEffect->displayName() : juce::String ("EMPTY"),
                                category, true, lines[0], lines[1]);
            board.setSelectedSlot (0);
            board.setPedalCount (1);
            board.setStatusText ("48 kHz    64 smp    ROUND TRIP 4.6 ms");

            auto slotView = std::make_unique<pb::SlotView> (0);
            slotView->setSlot (previewEffect != nullptr ? previewEffect->displayName() : juce::String(),
                               category, lines[0], lines[1], true);
            slotView->setSelected (true);
            slotView->setBounds (board.getSlotBounds (0).getSmallestIntegerContainer());
            board.addAndMakeVisible (*slotView);

            pb::GenericEffectEditor editor (previewRegistry);
            editor.setSlot (&slot);

            const auto area = board.editorBounds();
            editor.setBounds ((int) area.getX(), (int) area.getY(), (int) area.getWidth(), (int) area.getHeight());
            board.addAndMakeVisible (editor);

            // Card 3.4: the real macro strip, so a render shows the app rather than the mockup.
            board.setPaintedMacrosVisible (false);

            pb::MacroBank previewMacros (previewRegistry);
            pb::MacroStrip previewStrip (previewMacros, previewRegistry, nullptr);
            const auto macroArea = board.macroBounds();
            previewStrip.setBounds ((int) macroArea.getX(), (int) macroArea.getY(),
                                    (int) macroArea.getWidth(), (int) macroArea.getHeight());
            board.addAndMakeVisible (previewStrip);

            board.renderToPng (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]),
                               args.size() >= 3 ? args[2].getFloatValue() : 2.0f);
            quit();
            return;
        }

        // The sixteen real slot components on the board, without audio, for judging card 2.5's board:
        //   "pump BOARD.app/Contents/MacOS/pump BOARD" --render-slots slots.png [scale]
        if (args.size() >= 2 && args[0] == "--render-slots")
        {
            pb::BoardPrototype board;
            board.setSize (1100, 700);
            board.setPaintedSlotsVisible (false);       // the SlotViews draw the pedals, exactly as in the app
            board.setPedalCount (3);
            board.setStatusText ("48 kHz    64 smp    ROUND TRIP 4.6 ms");

            const char* const names[]  = { "pump TUBE", "pump ECHO", "pump SPRING" };
            const char* const cats[]   = { "DRIVE", "TIME", "SPACE" };
            const char* const first[]  = { "DRIVE 12.0 dB", "TIME L 500 ms", "DWELL 50 %" };
            const char* const second[] = { "MIX 100 %", "FEEDBACK 35 %", "DECAY 2.50 s" };

            // The header tempo field needs a "board/tempo" to read; the app gets one from the BoardProcessor.
            pb::ParameterRegistry renderRegistry;
            pb::ParameterRegistry::Entry tempoEntry;
            tempoEntry.id = "board/tempo";
            tempoEntry.displayName = "Tempo";
            tempoEntry.minValue = 20.0f;
            tempoEntry.maxValue = 300.0f;
            tempoEntry.defaultValue = 140.0f;
            renderRegistry.add (tempoEntry);

            pb::TempoField tempoField (renderRegistry);
            tempoField.setBounds (board.tempoBounds().getSmallestIntegerContainer());
            board.addAndMakeVisible (tempoField);

            std::vector<std::unique_ptr<pb::SlotView>> views;

            for (int i = 0; i < 16; ++i)
            {
                auto view = std::make_unique<pb::SlotView> (i);

                if (i < 3)
                    view->setSlot (names[i], cats[i], first[i], second[i], true);

                view->setSelected (i == 0);
                view->setBounds (board.getSlotBounds (i).getSmallestIntegerContainer());
                board.addAndMakeVisible (*view);
                views.push_back (std::move (view));
            }

            // Card 3.4: the real macro strip, so a render shows the app rather than the mockup.
            board.setPaintedMacrosVisible (false);

            pb::MacroBank renderMacros (renderRegistry);
            const float macroPositions[] = { 0.18f, 0.62f, 0.30f, 0.50f, 0.42f, 0.72f, 0.25f, 0.80f };

            for (int i = 0; i < pb::MacroBank::kNumMacros; ++i)
                renderRegistry.set (renderMacros.macroParamIndex (i), macroPositions[i]);

            pb::MacroStrip renderStrip (renderMacros, renderRegistry, nullptr);
            const auto macroArea = board.macroBounds();
            renderStrip.setBounds ((int) macroArea.getX(), (int) macroArea.getY(),
                                   (int) macroArea.getWidth(), (int) macroArea.getHeight());
            board.addAndMakeVisible (renderStrip);

            board.renderToPng (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]),
                               args.size() >= 3 ? args[2].getFloatValue() : 2.0f);
            quit();
            return;
        }

        pb::registerBuiltInEffects (pb::EffectRegistry::instance());

        registry = std::make_unique<pb::ParameterRegistry>();
        midiRouter = std::make_unique<pb::MidiRouter> (*registry);
        processor = std::make_unique<pb::BoardProcessor> (*registry, *midiRouter);
        audioEngine = std::make_unique<pb::AudioEngine> (*processor);

        if (! audioEngine->initialise())
            juce::Logger::writeToLog ("pump BOARD: " + audioEngine->getLastError());

        // Three pedals in the order a guitar chain wants them: drive, then delay, then space. Done before the
        // window exists so the editor panel opens on a real pedal rather than an empty slot.
        boardModel = std::make_unique<pb::BoardModel> (*processor, pb::EffectRegistry::instance(), *registry);

        const char* const chain[] = { "pump.tube", "pump.echo", "pump.spring" };

        for (int i = 0; i < 3; ++i)
            if (! boardModel->insert (i, chain[i]))
                juce::Logger::writeToLog (juce::String ("pump BOARD: could not create ") + chain[i]);

        juce::Logger::writeToLog ("pump BOARD: " + juce::String (boardModel->size()) + " slot(s) published");

        window = std::make_unique<pb::MainWindow> (*audioEngine, *processor, *registry, *boardModel, *midiRouter);
    }

    void shutdown() override
    {
        // Reverse order of construction: the window closes the device before the processors go.
        window = nullptr;
        boardModel = nullptr;
        audioEngine = nullptr;
        processor = nullptr;
        midiRouter = nullptr;
        registry = nullptr;

        releaseUi();
    }

    // The look-and-feel and the static typefaces must go while JUCE is still up: left to static destruction they
    // outlive the GUI shutdown (leak-detector noise, and a known crash with system typefaces).
    static void releaseUi()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        pb::style::fonts::labelTypeface() = nullptr;
        pb::style::fonts::monoTypeface() = nullptr;
        pb::style::fonts::boldTypeface() = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    pb::style::IdmLookAndFeel lookAndFeel;                 // declared first, so destroyed after the window
    std::unique_ptr<pb::ParameterRegistry> registry;
    std::unique_ptr<pb::MidiRouter> midiRouter;
    std::unique_ptr<pb::BoardProcessor> processor;
    std::unique_ptr<pb::AudioEngine> audioEngine;
    std::unique_ptr<pb::BoardModel> boardModel;
    std::unique_ptr<pb::MainWindow> window;
};

START_JUCE_APPLICATION (PumpBoardApplication)
