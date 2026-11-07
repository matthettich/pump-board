#include <juce_core/juce_core.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Presets/PresetLibrary.h"

class PresetLibraryTests final : public juce::UnitTest
{
public:
    PresetLibraryTests() : juce::UnitTest ("PresetLibrary", "presets") {}

    void runTest() override
    {
        beginTest ("a named board is saved, listed and loaded back");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("pump-board-preset-library-test");
            directory.deleteRecursively();

            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "pump.spring"));

            pb::PresetLibrary library (directory);
            expect (library.list().isEmpty());                    // an absent folder lists nothing, not a crash
            expect (library.save ("Clean tone", model, registry));

            const auto files = library.list();
            expectEquals (files.size(), 1);
            expectEquals (files[0].getFileNameWithoutExtension(), juce::String ("Clean tone"));

            pb::BoardModel reloaded (processor, effects, registry);
            expect (pb::loadBoard (files[0], reloaded, registry));
            expectEquals (reloaded.size(), 1);
            expectEquals (reloaded.at (0)->getEffect()->typeId(), juce::String ("pump.spring"));

            directory.deleteRecursively();
        }

        beginTest ("a typed name cannot escape the presets folder");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("pump-board-preset-library-test");
            pb::PresetLibrary library (directory);

            const auto file = library.fileFor ("../../../live / take 2");
            expectEquals (file.getParentDirectory().getFullPathName(), directory.getFullPathName());
            expect (! file.getFileNameWithoutExtension().containsChar ('/'));
        }

        beginTest ("an empty name is refused rather than writing a nameless board");
        {
            const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("pump-board-preset-library-test");

            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "pump.spring");

            pb::PresetLibrary library (directory);
            expect (! library.save ("   ", model, registry));
            expect (library.list().isEmpty());

            directory.deleteRecursively();
        }
    }
};

static PresetLibraryTests presetLibraryTests;
