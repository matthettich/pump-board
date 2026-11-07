#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/KeyRouter.h"
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Presets/BoardState.h"
#include "Presets/PresetManager.h"

namespace
{
juce::File testDirectory()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile ("pump-board-preset-manager-test");
}

// Only what a save and a load touch, wired the way MainContent wires it.
struct Rig
{
    pb::ParameterRegistry registry;
    pb::MidiRouter router { registry };
    pb::BoardProcessor processor { registry, router };
    pb::EffectRegistry effects;
    pb::BoardModel model;
    pb::MacroBank macros;
    pb::KeyRouter keys { router };
    pb::LearnController learn;

    Rig()
        : model (processor, effects, registry),
          macros (registry),
          learn (registry, router, keys)
    {
        processor.prepareToPlay (48000.0, 64);
        pb::registerBuiltInEffects (effects);
    }
};
} // namespace

class PresetManagerTests final : public juce::UnitTest
{
public:
    PresetManagerTests() : juce::UnitTest ("PresetManager", "presets") {}

    void runTest() override
    {
        beginTest ("the folder is the list, sorted by name, whatever order they were saved in");
        {
            const auto directory = testDirectory();
            directory.deleteRecursively();

            const pb::PresetManager manager (directory);
            expect (manager.list().isEmpty(), "an absent folder lists nothing rather than crashing");

            expect (manager.saveAs ("Zebra", {}).existsAsFile());
            expect (manager.saveAs ("alpha", {}).existsAsFile());
            expect (manager.saveAs ("Middle", {}).existsAsFile());

            const auto files = manager.list();
            expectEquals (files.size(), 3);
            expectEquals (manager.nameOf (files[0]), juce::String ("alpha"));
            expectEquals (manager.nameOf (files[1]), juce::String ("Middle"));
            expectEquals (manager.nameOf (files[2]), juce::String ("Zebra"));

            // A name that is only spaces is refused rather than writing a nameless board.
            expect (manager.saveAs ("   ", {}).getFullPathName().isEmpty());
            expectEquals (manager.list().size(), 3);

            directory.deleteRecursively();
        }

        beginTest ("a board file keeps its name, and a broken one says why");
        {
            const auto directory = testDirectory();
            directory.deleteRecursively();

            const pb::PresetManager manager (directory);

            pb::BoardData data;
            data.name = "wrong name on purpose";
            const auto file = manager.saveAs ("Evening set", data);
            expect (file.existsAsFile());

            pb::BoardData readBack;
            pb::LoadResult result;
            expect (manager.read (file, readBack, result));
            expectEquals (readBack.name, juce::String ("Evening set"));    // the file's own name, not the data's

            const auto broken = directory.getChildFile ("broken.board");
            broken.replaceWithText ("{\"schemaVersion\": 1, \"slots\": [");

            pb::BoardData nothing;
            const auto failed = manager.read (broken, nothing, result);
            expect (! failed);
            expect (result.error.isNotEmpty());

            const auto missing = manager.read (directory.getChildFile ("nope.board"), nothing, result);
            expect (! missing);
            expect (result.error.containsIgnoreCase ("no board file"));

            directory.deleteRecursively();
        }

        beginTest ("a typed name cannot escape the folder");
        {
            const pb::PresetManager manager (testDirectory());
            const auto file = manager.fileFor ("../../../live / take 2");

            expectEquals (file.getParentDirectory().getFullPathName(),
                          testDirectory().getFullPathName());
            expect (! file.getFileName().containsChar ('/'));
            expectEquals (file.getFileExtension(), juce::String (".board"));
        }

        beginTest ("a board with a macro and a mapping survives the whole trip through the file");
        {
            const auto directory = testDirectory();
            directory.deleteRecursively();

            Rig rig;
            expect (rig.model.insert (0, "pump.spring"));

            auto* slot = rig.model.at (0);
            expect (slot != nullptr);

            const auto params = slot->getEffect()->params();
            expect (! params.empty());

            const auto paramId = params[0].id;
            const int paramIndex = slot->paramIndices()[0];

            rig.macros.setName (1, "SPACE");
            rig.macros.setTargets (1, { { paramIndex, 0.0f, 1.0f, pb::MacroBank::Curve::Logarithmic } });
            rig.registry.set (rig.macros.macroParamIndex (1), 0.5f);
            rig.macros.update();

            pb::Mapping mapping;
            mapping.source = { pb::SourceType::Note, 0, 36 };
            mapping.action = pb::ActionType::Toggle;
            mapping.paramIndex = slot->onParamIndex();
            rig.learn.setMappings ({ mapping });

            const pb::PresetManager manager (directory);
            const auto captured = pb::capture (rig.model, rig.macros, rig.learn, rig.registry);
            const auto file = manager.saveAs ("With macros", captured);
            expect (file.existsAsFile());

            // A fresh launch in miniature: read the file, then rebuild the board from it.
            pb::BoardData readBack;
            pb::LoadResult result;
            expect (manager.read (file, readBack, result), result.error);
            expectEquals (result.warnings.size(), 0);
            expectEquals (readBack.name, juce::String ("With macros"));
            expectEquals ((int) readBack.macros[1].targets.size(), 1);
            expectEquals (readBack.macros[1].targets[0].paramId, paramId);
            expectEquals (readBack.macros[1].targets[0].curve, juce::String ("logarithmic"));
            expectEquals ((int) readBack.mappings.size(), 1);
            expectEquals (readBack.mappings[0].target, juce::String ("slot/0/on"));

            rig.model.clear();
            rig.learn.clearAll();
            rig.macros.setName (1, "");
            rig.macros.setTargets (1, {});

            const auto applied = pb::apply (readBack, rig.model, rig.macros, rig.learn, rig.registry);
            expect (applied.ok, applied.error);
            expectEquals (applied.warnings.size(), 0);

            expectEquals (rig.model.size(), 1);
            expectEquals (rig.macros.getName (1), juce::String ("SPACE"));
            expectEquals ((int) rig.macros.getTargets (1).size(), 1);
            expectEquals (rig.macros.getTargets (1)[0].paramIndex, rig.model.at (0)->paramIndices()[0]);
            expectEquals ((int) rig.learn.mappings().size(), 1);
            expectEquals (rig.learn.mappings()[0].paramIndex, rig.model.at (0)->onParamIndex());

            directory.deleteRecursively();
        }

        beginTest ("the working copy sits beside the presets folder, not inside it");
        {
            const auto working = pb::workingBoardFile();

            expectEquals (working.getFileExtension(), juce::String (".board"));
            expect (working.getParentDirectory() != pb::presetBoardsDirectory(),
                    "a working copy inside the folder would take a program-change number");
            expectEquals (working.getParentDirectory().getFileName(), juce::String ("pump BOARD"));
        }
    }
};

static PresetManagerTests presetManagerTests;
