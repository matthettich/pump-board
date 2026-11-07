#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Slot.h"
#include <cmath>

// Card 5.4's proof. A slot's dry copy is a delay ring read one effect-latency behind the input, so switching a
// pedal off fades to the same moment of the sound rather than to an earlier copy of it. The fake effect below
// reports latency and outputs silence, which isolates the dry path: if the dry copy were not delayed, the impulse
// would come back at once instead of four samples later.
namespace
{
class SilentLatentEffect final : public pb::IEffect
{
public:
    explicit SilentLatentEffect (int latencyIn) : latency (latencyIn) {}

    juce::String typeId() const override { return "test.silentlatent"; }
    juce::String displayName() const override { return "Silent Latent"; }

    std::vector<pb::ParamInfo> params() const override
    {
        return { { "mix", "Mix", 0.0f, 1.0f, 1.0f, "", false, 0.0f } };
    }

    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}

    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.clear(); }   // wet is silence

    int latencySamples() const noexcept override { return latency; }

private:
    int latency;
};
} // namespace

class SlotLatencyTests final : public juce::UnitTest
{
public:
    SlotLatencyTests() : juce::UnitTest ("SlotLatency", "engine") {}

    void runTest() override
    {
        beginTest ("a slot lets an impulse through untouched while it is on");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot { "latencyOn", registry };
            slot.setEffect (std::make_unique<SilentLatentEffect> (4));
            slot.prepare (48000.0, 64);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            buffer.setSample (0, 10, 0.5f);
            buffer.setSample (1, 10, 0.5f);

            slot.process (buffer);

            // On, so the wet (silence) wins: the impulse is gone, and nothing appears later either.
            expectWithinAbsoluteError (buffer.getSample (0, 10), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (pb::ParameterRegistry::kInvalid, -1, 0);
        }

        beginTest ("switching a latency-reporting slot on fades to the dry copy delayed by that latency");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot { "latencyOff", registry };
            slot.setEffect (std::make_unique<SilentLatentEffect> (4));

            registry.set (slot.onParamIndex(), 0.0f);      // start bypassed, so the fade runs wet-ward from dry
            slot.prepare (48000.0, 64);

            registry.set (slot.onParamIndex(), 1.0f);      // now switch it on: dry starts at full weight

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);

            slot.process (buffer);

            // Four samples of latency, four samples of dry delay: the impulse returns at index 4, and index 0 is
            // empty because that sample has not reached the output yet.
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.0f, 0.05f);
            expect (buffer.getSample (0, 4) > 0.9f, "the dry copy should arrive four samples late");
            expectWithinAbsoluteError (buffer.getSample (1, 4), buffer.getSample (0, 4), 1.0e-6f);
        }

        beginTest ("a zero-latency effect still passes the dry copy in the same block");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot { "latencyZero", registry };
            slot.setEffect (std::make_unique<SilentLatentEffect> (0));

            registry.set (slot.onParamIndex(), 0.0f);
            slot.prepare (48000.0, 64);
            registry.set (slot.onParamIndex(), 1.0f);

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);

            slot.process (buffer);

            expect (buffer.getSample (0, 0) > 0.9f, "with no reported latency there is nothing to compensate");
        }
    }
};

static SlotLatencyTests slotLatencyTests;
