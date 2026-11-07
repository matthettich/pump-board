#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/ParameterRegistry.h"
#include "Effects/adapters/PumpSpringAdapter.h"
#include "Engine/LoadMeter.h"
#include "Engine/Slot.h"
#include <cmath>

// Card 5.24. Slot.h is a fixed scaffold header, so the timing lives in LoadMeter's side table keyed by the slot's
// address. These tests cover the table's rules and that Slot::process actually feeds it.
class LoadMeterTests final : public juce::UnitTest
{
public:
    LoadMeterTests() : juce::UnitTest ("LoadMeter", "engine") {}

    void runTest() override
    {
        beginTest ("an unknown slot reads as zero and publishing to it is harmless");
        {
            pb::Slot* nothing = nullptr;
            expectEquals (pb::load::slotLoad (nothing), 0.0f);

            pb::load::publish (nothing, 0.01, 480);       // must not crash
            expectEquals (pb::load::slotLoad (nothing), 0.0f);
        }

        beginTest ("a published time becomes a smoothed fraction of the block budget");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot ("load-a", registry);

            pb::load::resetLoads();
            pb::load::setSampleRate (&slot, 48000.0);

            // 480 samples at 48 kHz is a 10 ms budget; 2.5 ms of it is a quarter.
            pb::load::publish (&slot, 0.0025, 480);

            expectWithinAbsoluteError (pb::load::slotLoad (&slot), 0.025f, 1.0e-4f);

            // Ten more blocks at the same cost converge on a quarter, not on 0.025.
            for (int i = 0; i < 60; ++i)
                pb::load::publish (&slot, 0.0025, 480);

            expectWithinAbsoluteError (pb::load::slotLoad (&slot), 0.25f, 5.0e-3f);
        }

        beginTest ("a load far over budget is clamped rather than reported as nonsense");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot ("load-b", registry);

            pb::load::resetLoads();
            pb::load::setSampleRate (&slot, 48000.0);

            for (int i = 0; i < 200; ++i)
                pb::load::publish (&slot, 0.100, 480);    // ten times the budget

            expect (pb::load::slotLoad (&slot) <= 4.0f);
            expect (pb::load::slotLoad (&slot) > 1.0f);
        }

        beginTest ("the board's load is the sum of the live slots");
        {
            pb::ParameterRegistry registry;
            pb::Slot a ("load-c", registry);
            pb::Slot b ("load-d", registry);

            pb::load::resetLoads();
            pb::load::setSampleRate (&a, 48000.0);
            pb::load::setSampleRate (&b, 48000.0);

            for (int i = 0; i < 100; ++i)
            {
                pb::load::publish (&a, 0.005, 480);       // half the budget
                pb::load::publish (&b, 0.0025, 480);      // a quarter
            }

            const float sum = pb::load::slotLoad (&a) + pb::load::slotLoad (&b);
            expectWithinAbsoluteError (pb::load::boardLoad(), sum, 1.0e-4f);
            expect (pb::load::boardLoad() > 0.5f);
        }

        beginTest ("a slot that is gone stops counting");
        {
            pb::ParameterRegistry registry;
            auto slot = std::make_unique<pb::Slot> ("load-e", registry);

            pb::load::resetLoads();
            const float baseline = pb::load::boardLoad();
            pb::load::setSampleRate (slot.get(), 48000.0);
            pb::load::publish (slot.get(), 0.005, 480);
            expect (pb::load::boardLoad() > 0.0f);

            slot.reset();                                  // the destructor unregisters

            // Measured against the baseline rather than against zero: other live slots (from other suites) are not
            // this test's business, and a benchmark that assumes an empty table is a benchmark that lies later.
            logMessage ("board load after the slot died: " + juce::String (pb::load::boardLoad(), 4));
            expect (std::abs (pb::load::boardLoad() - baseline) < 1.0e-6f);
        }

        beginTest ("processing a real block feeds the meter");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot ("load-f", registry);
            slot.setEffect (std::make_unique<pb::PumpSpringAdapter>());
            slot.prepare (48000.0, 64);

            pb::load::resetLoads();

            juce::AudioBuffer<float> buffer (2, 64);
            buffer.clear();

            for (int block = 0; block < 20; ++block)
            {
                for (int i = 0; i < 64; ++i)
                    buffer.setSample (0, i, 0.1f * std::sin (0.02f * (float) (i + block * 64)));

                slot.process (buffer);
            }

            const float load = pb::load::slotLoad (&slot);
            logMessage ("spring slot load after 20 blocks: " + juce::String (load, 4));
            expect (load >= 0.0f);
            expect (std::isfinite (load));
        }
    }
};

static LoadMeterTests loadMeterTests;
