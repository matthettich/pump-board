#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Common/AllocationGuard.h"
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include <vector>

class AllocationGuardTests final : public juce::UnitTest
{
public:
    AllocationGuardTests() : juce::UnitTest ("AllocationGuard", "engine") {}

    void runTest() override
    {
#ifdef PB_ALLOCATION_GUARD
        beginTest ("an allocation inside the scope is counted");
        {
            pb::alloc::resetViolations();

            {
                const pb::alloc::Scope scope;
                std::vector<int> inside (1000, 1);
                expect (inside.size() == 1000);
            }

            expect (pb::alloc::violations() > 0);
        }

        beginTest ("an allocation outside the scope is not counted");
        {
            pb::alloc::resetViolations();

            std::vector<int> outside (1000, 1);
            expect (outside.size() == 1000);

            expectEquals (pb::alloc::violations(), 0);
        }

        beginTest ("a block through a full board of pump pedals allocates nothing");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (48000.0, 64);

            pb::EffectRegistry effects;
            pb::registerBuiltInEffects (effects);

            pb::BoardModel model (processor, effects, registry);

            const char* const types[] = { "pump.spring", "pump.echo", "pump.tube" };

            for (int i = 0; i < pb::ChainSnapshot::kMaxSlots; ++i)
                expect (model.insert (i, types[i % 3]));

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            // Two blocks first, so any one-off work (a pending setSettings, a first-use table) happens outside
            // the measurement; the third block is steady state, which is what must be allocation-free.
            for (int block = 0; block < 3; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i)
                        buffer.setSample (ch, i, 0.1f * (float) ((i + block) % 7));

                pb::alloc::resetViolations();
                processor.processBlock (buffer, midi);
                const int during = pb::alloc::violations();

                if (block == 2)
                {
                    logMessage ("steady-state block allocations: " + juce::String (during));
                    expectEquals (during, 0);
                }
                else
                {
                    logMessage ("warm-up block " + juce::String (block) + " allocations: " + juce::String (during));
                }
            }
        }
#else
        beginTest ("the allocation guard is a Debug build feature");
        expect (pb::alloc::violations() == 0);
#endif
    }
};

static AllocationGuardTests allocationGuardTests;
