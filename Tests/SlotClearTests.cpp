#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"

namespace
{
class ClearGainEffect final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "test.gain"; }
    juce::String displayName() const override { return "Test Gain"; }

    std::vector<pb::ParamInfo> params() const override
    {
        return { { "gain", "Gain", 0.0f, 4.0f, 2.0f, "", false, 0.0f } };
    }

    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int index, float value) noexcept override { if (index == 0) gain = value; }
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (gain); }

private:
    float gain = 2.0f;
};

void fill (juce::AudioBuffer<float>& buffer, float value)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, value);
}
} // namespace

// Card 5.28: taking a pedal off the board. The request was "once added, you are stuck until you make a new board" —
// so what matters is that a position can be emptied, that the board does not renumber around it, and that the
// position can take a new pedal afterwards.
class SlotClearTests final : public juce::UnitTest
{
public:
    SlotClearTests() : juce::UnitTest ("SlotClear", "engine") {}

    void runTest() override
    {
        beginTest ("clearing a position keeps the position and the slot's identity");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<ClearGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "test.gain"));
            expect (model.insert (1, "test.gain"));

            auto* first = model.at (0);
            expect (first != nullptr);

            const auto uid = first->getUid();
            expectEquals (model.categoryAt (0), juce::String ("utility"));

            expect (model.clearSlot (0));

            expectEquals (model.size(), 2);                        // no renumbering, no gap closed
            expect (model.at (0) != nullptr);
            expect (model.at (0)->getEffect() == nullptr);         // the pedal is gone
            expectEquals (model.at (0)->getUid(), uid);            // the position kept its identity
            expectEquals (model.categoryAt (0), juce::String());   // and its category went with the pedal
            expect (model.at (1)->getEffect() != nullptr);         // the neighbour is untouched
        }

        beginTest ("a cleared position passes audio and leaves its neighbour working");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<ClearGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "test.gain");
            model.insert (1, "test.gain");

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            model.clearSlot (0);
            processor.collectGarbage();

            fill (buffer, 0.25f);
            processor.processBlock (buffer, midi);

            // Two gains would read 1.0; with the first one gone, the dry signal passes the empty slot unchanged
            // and only the second pedal doubles it.
            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.5f, 1.0e-3f);
        }

        beginTest ("the cleared pedal's parameters leave the registry");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<ClearGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "test.gain");

            const auto id = "slot/" + model.at (0)->getUid() + "/gain";
            expect (registry.find (id) != pb::ParameterRegistry::kInvalid);

            expect (model.clearSlot (0));

            expect (registry.find (id) == pb::ParameterRegistry::kInvalid);

            // The position's own switch is not the pedal's: it belongs to the slot and outlives the pedal, so an
            // emptied position can be switched on and off before anything is put back in it.
            expect (registry.find ("slot/" + model.at (0)->getUid() + "/on") != pb::ParameterRegistry::kInvalid);
        }

        beginTest ("an emptied position takes a new pedal, so a board is never stuck");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<ClearGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "test.gain");
            model.insert (1, "test.gain");

            expect (model.clearSlot (0));
            expect (model.replace (0, "test.gain"));
            expect (model.at (0)->getEffect() != nullptr);
            expectEquals (model.size(), 2);
        }

        beginTest ("clearing an empty position or a bad index is refused, not a crash");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<ClearGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "test.gain");
            model.clearSlot (0);

            expect (! model.clearSlot (0));           // already empty
            expect (! model.clearSlot (-1));
            expect (! model.clearSlot (9));
            expectEquals (model.size(), 1);
        }
    }
};

static SlotClearTests slotClearTests;
