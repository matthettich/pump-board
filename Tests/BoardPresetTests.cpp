#include <juce_core/juce_core.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Presets/BoardPreset.h"

namespace
{
int paramIndex (pb::Slot* slot, const juce::String& id)
{
    if (slot == nullptr || slot->getEffect() == nullptr)
        return pb::ParameterRegistry::kInvalid;

    const auto params = slot->getEffect()->params();
    const auto& indices = slot->paramIndices();

    for (size_t i = 0; i < params.size() && i < indices.size(); ++i)
        if (params[i].id == id)
            return indices[i];

    return pb::ParameterRegistry::kInvalid;
}
} // namespace

class BoardPresetTests final : public juce::UnitTest
{
public:
    BoardPresetTests() : juce::UnitTest ("BoardPreset", "presets") {}

    void runTest() override
    {
        beginTest ("a board survives a round trip through its file");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "pump.tube"));
            expect (model.insert (1, "pump.echo"));
            expect (model.insert (2, "pump.spring"));

            // Move two values off their defaults and switch the echo off, so a lazy save would be caught.
            registry.set (paramIndex (model.at (0), "drive"), 27.0f);
            registry.set (paramIndex (model.at (1), "feedback"), 61.0f);
            registry.set (model.at (1)->onParamIndex(), 0.0f);

            const int tempoIndex = registry.find ("board/tempo");
            expect (tempoIndex != pb::ParameterRegistry::kInvalid);
            registry.set (tempoIndex, 137.0f);

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pump-board-preset-test.xml");
            file.deleteFile();
            expect (pb::saveBoard (file, model, registry));
            expect (file.existsAsFile());

            pb::BoardModel reloaded (processor, effects, registry);
            expect (pb::loadBoard (file, reloaded, registry));

            expectEquals (reloaded.size(), 3);
            expectEquals (reloaded.at (0)->getEffect()->typeId(), juce::String ("pump.tube"));
            expectEquals (reloaded.at (1)->getEffect()->typeId(), juce::String ("pump.echo"));
            expectEquals (reloaded.at (2)->getEffect()->typeId(), juce::String ("pump.spring"));
            expectEquals (reloaded.categoryAt (2), juce::String ("space"));

            expectWithinAbsoluteError (registry.get (paramIndex (reloaded.at (0), "drive")), 27.0f, 1.0e-3f);
            expectWithinAbsoluteError (registry.get (paramIndex (reloaded.at (1), "feedback")), 61.0f, 1.0e-3f);
            expect (registry.get (reloaded.at (1)->onParamIndex()) < 0.5f);      // still switched off
            expectWithinAbsoluteError (registry.get (paramIndex (reloaded.at (2), "mix")), 30.0f, 1.0e-3f);

            // The tempo travels with the board, so a saved synced delay does not come back at 120.
            expectWithinAbsoluteError (registry.get (tempoIndex), 137.0f, 1.0e-3f);

            file.deleteFile();
        }

        beginTest ("the order of the chain is what the file says");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "pump.spring"));
            expect (model.insert (1, "pump.tube"));

            const std::unique_ptr<juce::XmlElement> xml (pb::boardToXml (model, registry));
            pb::BoardModel copy (processor, effects, registry);
            expect (pb::boardFromXml (*xml, copy, registry));

            expectEquals (copy.size(), 2);
            expectEquals (copy.at (0)->getEffect()->typeId(), juce::String ("pump.spring"));
            expectEquals (copy.at (1)->getEffect()->typeId(), juce::String ("pump.tube"));
        }

        beginTest ("a missing file leaves the board alone");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "pump.spring"));

            const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("pump-board-no-such-preset.xml");
            missing.deleteFile();

            expect (! pb::loadBoard (missing, model, registry));
            expectEquals (model.size(), 1);
        }

        beginTest ("a foreign file is refused rather than half-loaded");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "pump.spring"));

            juce::XmlElement other ("SOMETHING_ELSE");
            expect (! pb::boardFromXml (other, model, registry));
            expectEquals (model.size(), 1);
        }
    }
};

static BoardPresetTests boardPresetTests;
