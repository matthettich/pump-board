// 7 Nov 2026: one NaN at the input left pump TWANG silent until it was rebuilt (its springs' feedback went NaN and
// the safety limiter turned the output to zero). The board now scrubs its input and every slot its own output.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include <limits>

namespace
{
// A pedal that turns its whole block into NaN: the worst neighbour a pedal can have.
class NanFx final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "test.nan"; }
    juce::String displayName() const override { return "NaN"; }
    std::vector<pb::ParamInfo> params() const override { return {}; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>& buffer) noexcept override
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), std::numeric_limits<float>::quiet_NaN(),
                                               buffer.getNumSamples());
    }
};
} // namespace

class NonFiniteTests final : public juce::UnitTest
{
public:
    NonFiniteTests() : juce::UnitTest ("NonFinite", "engine") {}

    // RMS of the board's output over 0.5 s of quiet noise (the first blocks skipped)
    static double outputRms (pb::BoardProcessor& processor, juce::Random& rng)
    {
        juce::MidiBuffer midi;
        double sum = 0.0;
        int count = 0;

        for (int b = 0; b < 94; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 256);

            for (int i = 0; i < 256; ++i)
            {
                const float v = (rng.nextFloat() * 2.0f - 1.0f) * 0.1f;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            processor.processBlock (buffer, midi);

            if (b > 10)
                for (int i = 0; i < 256; ++i, ++count)
                    sum += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
        }

        return std::sqrt (sum / juce::jmax (1, count));
    }

    static void sendBadBlock (pb::BoardProcessor& processor, float bad)
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        buffer.setSample (0, 10, bad);
        buffer.setSample (1, 10, bad);
        processor.processBlock (buffer, midi);
    }

    void runTest() override
    {
        beginTest ("TWANG keeps ringing after a NaN or an infinity at the input");
        {
            juce::Random rng (7);
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);
            processor.prepareToPlay (48000.0, 256);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "pump.twang"));
            registry.set (registry.find ("slot/" + model.at (0)->getUid() + "/mix"), 100.0f);   // springs only

            const double before = outputRms (processor, rng);
            expect (before > 0.005, "TWANG rings on noise (" + juce::String (before, 5) + ")");

            sendBadBlock (processor, std::numeric_limits<float>::quiet_NaN());
            const double afterNan = outputRms (processor, rng);
            expect (afterNan > 0.5 * before, "still ringing after a NaN (" + juce::String (afterNan, 5) + ")");

            sendBadBlock (processor, std::numeric_limits<float>::infinity());
            const double afterInf = outputRms (processor, rng);
            expect (afterInf > 0.5 * before, "still ringing after an infinity (" + juce::String (afterInf, 5) + ")");
        }

        beginTest ("a pedal that outputs NaN does not poison the pedal after it");
        {
            juce::Random rng (8);
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);
            effects.add ({ "test.nan", "NaN", "utility", [] { return std::make_unique<NanFx>(); } });
            processor.prepareToPlay (48000.0, 256);

            pb::BoardModel model (processor, effects, registry);
            expect (model.insert (0, "test.nan"));
            expect (model.insert (1, "pump.twang"));
            const auto twangUid = model.at (1)->getUid();
            registry.set (registry.find ("slot/" + twangUid + "/mix"), 100.0f);

            outputRms (processor, rng);                 // TWANG sees only zeros, never a NaN
            expect (model.clearSlot (0));               // the bad pedal goes; the noise reaches TWANG again

            const double after = outputRms (processor, rng);
            expect (after > 0.005, "TWANG rings once the bad pedal is gone (" + juce::String (after, 5) + ")");
        }
    }
};

static NonFiniteTests nonFiniteTests;
