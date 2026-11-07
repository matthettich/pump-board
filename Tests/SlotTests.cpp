#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Slot.h"
#include <cmath>

namespace
{
// A gain of 2, so wet and dry are obviously different and any click is easy to see.
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

    void process (juce::AudioBuffer<float>& buffer) noexcept override
    {
        buffer.applyGain (gain);
    }

private:
    float gain = 2.0f;
};
} // namespace

class SlotTests final : public juce::UnitTest
{
public:
    SlotTests() : juce::UnitTest ("Slot", "engine") {}

    void runTest() override
    {
        beginTest ("an on slot applies its effect");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot ("a", registry);
            slot.setEffect (std::make_unique<TestGainEffect>());
            slot.prepare (48000.0, 64);

            expect (slot.onParamIndex() != pb::ParameterRegistry::kInvalid);
            expectEquals ((int) slot.paramIndices().size(), 1);
            registry.set (slot.onParamIndex(), 1.0f);

            juce::AudioBuffer<float> buffer (2, 64);
            for (int i = 0; i < 64; ++i)
            { buffer.setSample (0, i, 0.25f); buffer.setSample (1, i, 0.25f); }

            slot.process (buffer);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.5f, 1.0e-4f);
        }

        beginTest ("switching off returns to dry with no step");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot ("b", registry);
            slot.setEffect (std::make_unique<TestGainEffect>());
            slot.prepare (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            float previous = 0.25f;
            float largestStep = 0.0f;
            bool sawWet = false, sawDry = false;

            auto runBlock = [&]
            {
                for (int i = 0; i < 64; ++i)
                { buffer.setSample (0, i, 0.25f); buffer.setSample (1, i, 0.25f); }

                slot.process (buffer);

                for (int i = 0; i < 64; ++i)
                {
                    const float sample = buffer.getSample (0, i);
                    largestStep = juce::jmax (largestStep, std::abs (sample - previous));
                    previous = sample;
                    sawWet = sawWet || sample > 0.45f;
                    sawDry = sawDry || std::abs (sample - 0.25f) < 1.0e-4f;
                }
            };

            registry.set (slot.onParamIndex(), 1.0f);
            for (int i = 0; i < 4; ++i) runBlock();          // settled wet: 0.5

            // Measure the switch itself: start from the settled wet level, not from the first block.
            previous = 0.5f;
            largestStep = 0.0f;

            registry.set (slot.onParamIndex(), 0.0f);
            for (int i = 0; i < 40; ++i) runBlock();         // 40 blocks = 2560 samples, well past 10 ms

            expect (sawWet);
            expect (sawDry);
            expect (previous < 0.2501f);
            expect (largestStep < 0.05f);                    // no click: the card's threshold
        }

        beginTest ("an empty slot changes nothing");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot ("c", registry);
            slot.prepare (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            for (int i = 0; i < 64; ++i)
                buffer.setSample (0, i, 0.3f);

            slot.process (buffer);

            expectWithinAbsoluteError (buffer.getSample (0, 63), 0.3f, 1.0e-6f);
        }

        beginTest ("a slot unregisters its params when it dies");
        {
            pb::ParameterRegistry registry;
            {
                pb::Slot slot ("d", registry);
                slot.setEffect (std::make_unique<TestGainEffect>());
                expect (registry.find ("slot/d/on") != pb::ParameterRegistry::kInvalid);
                expect (registry.find ("slot/d/gain") != pb::ParameterRegistry::kInvalid);
            }

            expectEquals (registry.find ("slot/d/on"), pb::ParameterRegistry::kInvalid);
            expectEquals (registry.find ("slot/d/gain"), pb::ParameterRegistry::kInvalid);
        }
    }
};

static SlotTests slotTests;
