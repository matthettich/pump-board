#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/TempoAware.h"
#include "Effects/adapters/PumpSwirlAdapter.h"
#include "Effects/adapters/PumpGateAdapter.h"
#include "Effects/adapters/PumpGrainAdapter.h"
#include "Effects/adapters/PumpStutterAdapter.h"
#include "Effects/adapters/PumpChopAdapter.h"
#include "Effects/adapters/PumpSpringAdapter.h"
#include "Effects/adapters/PumpVerbAdapter.h"
#include "Engine/BoardProcessor.h"
#include "Engine/Slot.h"
#include <cmath>
#include <memory>

// Card 5.22 (the tempo hook). Before it, five pedals held a `setTempo()` nothing called and free-ran at their
// adapter's own 120 BPM. The board already had the route — BoardProcessor hands "board/tempo" to whichever slot
// effects implement ITempoAware, and only when it changes — so the fix was opting the pedals in. These tests prove
// both halves: the cast works for each pedal, and the value the board publishes arrives in the pedal.
class TempoReachTests final : public juce::UnitTest
{
public:
    TempoReachTests() : juce::UnitTest ("TempoReach", "engine") {}

    void runTest() override
    {
        beginTest ("every pedal that takes a tempo is reachable as ITempoAware");
        {
            pb::PumpSwirlAdapter swirl;
            pb::PumpGateAdapter gate;
            pb::PumpGrainAdapter grain;
            pb::PumpStutterAdapter stutter;
            pb::PumpChopAdapter chop;
            pb::PumpVerbAdapter verb;

            expect (dynamic_cast<pb::ITempoAware*> (&swirl) != nullptr);
            expect (dynamic_cast<pb::ITempoAware*> (&gate) != nullptr);
            expect (dynamic_cast<pb::ITempoAware*> (&grain) != nullptr);
            expect (dynamic_cast<pb::ITempoAware*> (&stutter) != nullptr);
            expect (dynamic_cast<pb::ITempoAware*> (&chop) != nullptr);
            expect (dynamic_cast<pb::ITempoAware*> (&verb) != nullptr);
        }

        beginTest ("setTempo lands in each pedal's own transport, with pump's clamps");
        {
            pb::PumpSwirlAdapter swirl;
            pb::PumpGateAdapter gate;
            pb::PumpGrainAdapter grain;
            pb::PumpStutterAdapter stutter;
            pb::PumpChopAdapter chop;

            pb::ITempoAware* swirlT = &swirl;
            pb::ITempoAware* gateT = &gate;
            pb::ITempoAware* grainT = &grain;
            pb::ITempoAware* stutterT = &stutter;
            pb::ITempoAware* chopT = &chop;

            for (pb::ITempoAware* t : { swirlT, gateT, grainT, stutterT, chopT })
                t->setTempo (90.0);

            expectWithinAbsoluteError (swirl.tempo(), 90.0, 1.0e-9);   // SWIRL exposes its tempo, not its transport
            expectWithinAbsoluteError (gate.currentTransport().bpm, 90.0, 1.0e-9);
            expectWithinAbsoluteError (grain.currentTransport().bpm, 90.0, 1.0e-9);
            expectWithinAbsoluteError (stutter.currentTransport().bpm, 90.0, 1.0e-9);
            expectWithinAbsoluteError (chop.currentTransport().bpm, 90.0, 1.0e-9);

            // SWIRL, GATE, GRAIN and STUTTER clamp to 20..999; CHOP to 40..240. The board's own param is 20..300,
            // so a board tempo of 300 reaches CHOP as 240. That is CHOP's range, not a bug here.
            gateT->setTempo (5000.0);
            expectWithinAbsoluteError (gate.currentTransport().bpm, 999.0, 1.0e-9);

            chopT->setTempo (300.0);
            expectWithinAbsoluteError (chop.currentTransport().bpm, 240.0, 1.0e-9);

            gateT->setTempo (1.0);
            expectWithinAbsoluteError (gate.currentTransport().bpm, 20.0, 1.0e-9);
        }

        beginTest ("a nonsense tempo is refused rather than passed on");
        {
            pb::PumpGateAdapter gate;
            pb::ITempoAware* gateT = &gate;

            gateT->setTempo (90.0);
            gateT->setTempo (-5.0);                     // clamped down to 20, not accepted as a negative
            expect (gate.currentTransport().bpm >= 20.0);
            expect (std::isfinite (gate.currentTransport().bpm));
        }

        beginTest ("the board's tempo reaches a gate slot and a chop slot");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            auto gate = std::make_unique<pb::PumpGateAdapter>();
            auto* rawGate = gate.get();
            auto chop = std::make_unique<pb::PumpChopAdapter>();
            auto* rawChop = chop.get();

            pb::Slot gateSlot ("tempo-gate", registry);
            gateSlot.setEffect (std::move (gate));
            gateSlot.prepare (48000.0, 64);

            pb::Slot chopSlot ("tempo-chop", registry);
            chopSlot.setEffect (std::move (chop));
            chopSlot.prepare (48000.0, 64);

            auto snapshot = std::make_unique<pb::ChainSnapshot>();
            snapshot->order[0] = &gateSlot;
            snapshot->order[1] = &chopSlot;
            snapshot->count = 2;
            processor.publish (std::move (snapshot));

            const int tempo = registry.find ("board/tempo");
            expect (tempo != pb::ParameterRegistry::kInvalid);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            registry.set (tempo, 132.0f);
            buffer.clear();
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (rawGate->currentTransport().bpm, 132.0, 1.0e-9);
            expectWithinAbsoluteError (rawChop->currentTransport().bpm, 132.0, 1.0e-9);

            // The board's ceiling is 300; CHOP's own range stops at 240, and that is the value it holds.
            registry.set (tempo, 300.0f);
            buffer.clear();
            processor.processBlock (buffer, midi);

            expectWithinAbsoluteError (rawGate->currentTransport().bpm, 300.0, 1.0e-9);
            expectWithinAbsoluteError (rawChop->currentTransport().bpm, 240.0, 1.0e-9);

            // A steady tempo does not re-send, but the pedals must still process a block without complaint.
            buffer.clear();
            for (int i = 0; i < 64; ++i)
                buffer.setSample (0, i, 0.2f * std::sin (0.05f * (float) i));

            processor.processBlock (buffer, midi);

            for (int i = 0; i < 64; ++i)
                expect (std::isfinite (buffer.getSample (0, i)));
        }

        beginTest ("a pedal with no tempo still gets through a block untouched by the route");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            auto spring = std::make_unique<pb::PumpSpringAdapter>();

            pb::Slot slot ("tempo-spring", registry);
            slot.setEffect (std::move (spring));
            slot.prepare (48000.0, 64);

            auto snapshot = std::make_unique<pb::ChainSnapshot>();
            snapshot->order[0] = &slot;
            snapshot->count = 1;
            processor.publish (std::move (snapshot));

            registry.set (registry.find ("board/tempo"), 75.0f);

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;
            buffer.clear();

            for (int i = 0; i < 64; ++i)
                buffer.setSample (0, i, 0.1f);

            processor.processBlock (buffer, midi);

            for (int i = 0; i < 64; ++i)
                expect (std::isfinite (buffer.getSample (0, i)));
        }
    }
};

static TempoReachTests tempoReachTests;
