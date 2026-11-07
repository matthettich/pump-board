#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"

namespace
{
class TestGainEffect final : public pb::IEffect
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

// A second pedal with the same param id but a different default, so a replace has to bring a new value with it.
class TestGain3Effect final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "test.gain3"; }
    juce::String displayName() const override { return "Test Gain Three"; }

    std::vector<pb::ParamInfo> params() const override
    {
        return { { "gain", "Gain", 0.0f, 4.0f, 3.0f, "", false, 0.0f } };
    }

    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int index, float value) noexcept override { if (index == 0) gain = value; }
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (gain); }

private:
    float gain = 3.0f;
};

void fillConstant (juce::AudioBuffer<float>& buffer, float value)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, value);
}
} // namespace

class BoardModelTests final : public juce::UnitTest
{
public:
    BoardModelTests() : juce::UnitTest ("BoardModel", "engine") {}

    void runTest() override
    {
        beginTest ("a published slot is heard on the audio path");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<TestGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "test.gain"));
            expectEquals (model.size(), 1);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.25f);
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.5f, 1.0e-3f);
        }

        beginTest ("clearing the board returns the audio to dry");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<TestGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "test.gain");

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.25f);
            processor.processBlock (buffer, midi);

            model.clear();
            processor.collectGarbage();

            fillConstant (buffer, 0.25f);
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.25f, 1.0e-3f);
        }

        beginTest ("sixteen slots fit, the seventeenth is refused");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<TestGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);

            for (int i = 0; i < pb::ChainSnapshot::kMaxSlots; ++i)
                expect (model.insert (model.size(), "test.gain"));

            expectEquals (model.size(), pb::ChainSnapshot::kMaxSlots);
            expect (! model.insert (0, "test.gain"));
            expectEquals (model.size(), pb::ChainSnapshot::kMaxSlots);
        }

        beginTest ("moving keeps the same slot object");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<TestGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);
            model.insert (0, "test.gain");
            model.insert (1, "test.gain");

            auto* first = model.at (0);
            const auto uid = first->getUid();

            expect (model.move (0, 1));
            expect (model.at (1) == first);
            expectEquals (model.at (1)->getUid(), uid);
            expectEquals (model.indexOfUid (uid), 1);
        }

        beginTest ("three slots in a row are all heard, in series");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<TestGainEffect>(); } });

            pb::BoardModel model (processor, effects, registry);

            for (int i = 0; i < 3; ++i)
                expect (model.insert (i, "test.gain"));

            // 0.1 x 2 x 2 x 2 = 0.8, below the board's ceiling, so nothing is clipped and every slot counted.
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.1f);
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.8f, 2.0e-3f);
            expectWithinAbsoluteError (buffer.getSample (1, 63), 0.8f, 2.0e-3f);
        }

        beginTest ("replacing a slot keeps its identity but takes the new pedal's defaults");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.gain", "Test Gain", "utility", [] { return std::make_unique<TestGainEffect>(); } });
            effects.add ({ "test.gain3", "Test Gain Three", "utility", [] { return std::make_unique<TestGain3Effect>(); } });

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "test.gain"));

            const auto uid = model.at (0)->getUid();
            expect (model.replace (0, "test.gain3"));

            expectEquals (model.at (0)->getUid(), uid);
            expectEquals (model.at (0)->getEffect()->typeId(), juce::String ("test.gain3"));

            // The new pedal's own default (3), not the value the pedal it replaced had (2).
            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            fillConstant (buffer, 0.1f);
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.3f, 2.0e-3f);
        }

        beginTest ("an unknown effect type is refused");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::BoardModel model (processor, effects, registry);

            expect (! model.insert (0, "pump.doesNotExist"));
            expectEquals (model.size(), 0);
        }
    }
};

static BoardModelTests boardModelTests;
