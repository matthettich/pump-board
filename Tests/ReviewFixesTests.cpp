// The Nov 2026 review's fixes, each pinned by the scenario that used to go wrong (docs/NOTES.md, review pass).
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Effects/TempoAware.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Engine/Slot.h"

namespace
{
class GainFx final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "review.gain"; }
    juce::String displayName() const override { return "Review Gain"; }
    std::vector<pb::ParamInfo> params() const override { return { { "gain", "Gain", 0.0f, 4.0f, 2.0f, "", false, 0.0f } }; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int index, float value) noexcept override { if (index == 0) gain = value; }
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (gain); }

private:
    float gain = 2.0f;
};

// Remembers the last tempo it was handed.
class TempoFx final : public pb::IEffect, public pb::ITempoAware
{
public:
    juce::String typeId() const override { return "review.tempo"; }
    juce::String displayName() const override { return "Review Tempo"; }
    std::vector<pb::ParamInfo> params() const override { return {}; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>&) noexcept override {}
    void setTempo (double bpm) noexcept override { tempo = bpm; }

    double tempo = 120.0;
};

// Outputs silence and reports latency, so what comes out of a bypassed slot is only its dry path.
class LatentSilentFx final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "review.latent"; }
    juce::String displayName() const override { return "Review Latent"; }
    std::vector<pb::ParamInfo> params() const override { return {}; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.clear(); }
    int latencySamples() const noexcept override { return 4; }
};

struct Rig
{
    pb::ParameterRegistry registry;
    pb::MidiRouter router { registry };
    pb::BoardProcessor processor { registry, router };
    pb::EffectRegistry effects;
    std::unique_ptr<pb::BoardModel> model;

    Rig()
    {
        processor.prepareToPlay (48000.0, 64);
        effects.add ({ "review.gain", "Review Gain", "utility", [] { return std::make_unique<GainFx>(); } });
        effects.add ({ "review.tempo", "Review Tempo", "time", [] { return std::make_unique<TempoFx>(); } });
        model = std::make_unique<pb::BoardModel> (processor, effects, registry);
    }

    float runConstant (float value)
    {
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), value, 64);

        processor.processBlock (buffer, midi);
        return buffer.getSample (0, 63);
    }
};
} // namespace

class ReviewFixesTests final : public juce::UnitTest
{
public:
    ReviewFixesTests() : juce::UnitTest ("ReviewFixes", "engine") {}

    void runTest() override
    {
        beginTest ("replace publishes a new slot (the live one is never touched) that keeps the uid and the switch");
        {
            Rig rig;
            expect (rig.model->insert (0, "review.gain"));
            rig.runConstant (0.25f);

            auto* before = rig.model->at (0);
            const auto uid = before->getUid();
            const int onIndex = before->onParamIndex();
            auto* beforeEffect = before->getEffect();

            expect (rig.model->replace (0, "review.gain"));
            auto* after = rig.model->at (0);

            expect (after != before, "a successor slot, not the same object mutated");
            expect (before->getEffect() == beforeEffect, "the old slot keeps its pedal until it is retired");
            expectEquals (after->getUid(), uid);
            expectEquals (after->onParamIndex(), onIndex);
            expect (rig.registry.info (onIndex).inUse, "the switch survives the swap");
            expect (after->paramIndices()[0] != pb::ParameterRegistry::kInvalid, "the new pedal registered its params");

            // The new chain plays, and the retired slot goes once two more blocks have run.
            expectWithinAbsoluteError (rig.runConstant (0.25f), 0.5f, 1.0e-3f);
            rig.runConstant (0.25f);
            rig.processor.collectGarbage();
            expect (rig.registry.info (onIndex).inUse, "retiring the old slot left the adopted switch alone");
        }

        beginTest ("clearing a slot keeps its position and switch, and the audio goes dry");
        {
            Rig rig;
            rig.model->insert (0, "review.gain");
            const int onIndex = rig.model->at (0)->onParamIndex();

            expect (rig.model->clearSlot (0));
            expectEquals (rig.model->size(), 1);
            expect (rig.model->at (0)->getEffect() == nullptr);
            expectEquals (rig.model->at (0)->onParamIndex(), onIndex);
            expectWithinAbsoluteError (rig.runConstant (0.25f), 0.25f, 1.0e-3f);
        }

        beginTest ("insertEmpty adds an empty position that passes audio");
        {
            Rig rig;
            expect (rig.model->insertEmpty (0));
            expectEquals (rig.model->size(), 1);
            expect (rig.model->at (0)->getEffect() == nullptr);
            expectWithinAbsoluteError (rig.runConstant (0.25f), 0.25f, 1.0e-3f);
        }

        beginTest ("prepareAll re-prepares off the live chain and puts the board back");
        {
            Rig rig;
            rig.model->insert (0, "review.gain");
            rig.runConstant (0.25f);
            rig.model->prepareAll();          // the device is "running" (prepared): the wait ends by timeout here
            expectWithinAbsoluteError (rig.runConstant (0.25f), 0.5f, 1.0e-3f);
        }

        beginTest ("a tempo-aware pedal inserted after the tempo moved gets the board's tempo");
        {
            Rig rig;
            rig.runConstant (0.0f);
            rig.registry.set (rig.registry.find ("board/tempo"), 90.0f);
            rig.runConstant (0.0f);           // the tempo is pushed to an empty chain: nothing receives it

            rig.model->insert (0, "review.tempo");
            rig.runConstant (0.0f);

            auto* fx = dynamic_cast<TempoFx*> (rig.model->at (0)->getEffect());
            expect (fx != nullptr);
            expectWithinAbsoluteError (fx->tempo, 90.0, 1.0e-6);
        }

        beginTest ("a learn capture takes one message; the rest of the block acts normally");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);

            pb::ParameterRegistry::Entry entry;
            entry.id = "test/toggle";
            entry.displayName = "toggle";
            entry.isToggle = true;
            const int toggle = registry.add (entry);

            auto table = std::make_unique<pb::MappingTable>();
            pb::Mapping mapping;
            mapping.source = { pb::SourceType::CC, 0, 20 };
            mapping.action = pb::ActionType::CCThreshold;          // (Toggle acts on notes and keys only)
            mapping.paramIndex = toggle;
            table->mappings.push_back (mapping);
            router.publish (std::move (table));

            router.beginLearn();

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);      // captured
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 1);           // must act
            router.handle (midi);

            int captured = 0;
            router.drainEvents ([&captured] (const pb::ControlEvent& e)
            {
                if (e.kind == pb::ControlEvent::Kind::LearnCaptured)
                    ++captured;
            });

            expectEquals (captured, 1);
            expectWithinAbsoluteError (registry.get (toggle), 1.0f, 1.0e-6f);
        }

        beginTest ("a settled-off latency pedal keeps its latency: no jump when its fade ends");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot { "reviewLatent", registry };
            slot.setEffect (std::make_unique<LatentSilentFx>());
            registry.set (slot.onParamIndex(), 0.0f);
            slot.prepare (48000.0, 16);                 // starts off and settled

            juce::AudioBuffer<float> buffer (2, 16);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            slot.process (buffer);

            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (buffer.getSample (0, 4), 1.0f, 1.0e-6f);
        }
    }
};

static ReviewFixesTests reviewFixesTests;
