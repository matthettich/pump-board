#include <juce_core/juce_core.h>
#include "Effects/IEffect.h"
#include "Effects/adapters/PumpSpringAdapter.h"
#include "Effects/adapters/PumpEchoAdapter.h"
#include "Effects/adapters/PumpTubeAdapter.h"
#include "Effects/adapters/PumpCrushAdapter.h"
#include "Effects/adapters/PumpSwirlAdapter.h"
#include "Effects/adapters/PumpCompAdapter.h"
#include "Effects/adapters/PumpHeatAdapter.h"
#include "Effects/adapters/PumpToneAdapter.h"
#include "Effects/adapters/PumpVerbAdapter.h"
#include "Effects/adapters/PumpLimitAdapter.h"
#include "Effects/adapters/PumpSnapAdapter.h"
#include "Effects/adapters/PumpStripAdapter.h"
#include "Effects/adapters/PumpCalmAdapter.h"
#include "Effects/adapters/PumpGateAdapter.h"
#include "Effects/adapters/PumpGrainAdapter.h"
#include "Effects/adapters/PumpStutterAdapter.h"
#include "Effects/adapters/PumpChopAdapter.h"
#include "Effects/adapters/PumpWideAdapter.h"
#include "Effects/adapters/PumpBandAdapter.h"
#include "Effects/adapters/PumpEqAdapter.h"
#include "Effects/adapters/PumpTwangAdapter.h"
#include <cmath>

// Card 5.23. pump names its modes ("Short tank", "1/4D", "Triode"); the board showed "2.00". ParamInfo now carries
// optional choices, and every adapter takes them from pump's own arrays. These tests cover the lookup rules and then
// hold every adapter to one invariant: a param with choices must have exactly as many names as it has values.
class ParamChoicesTests final : public juce::UnitTest
{
public:
    ParamChoicesTests() : juce::UnitTest ("ParamChoices", "effects") {}

    void runTest() override
    {
        beginTest ("a param with no choices behaves exactly as before");
        {
            pb::ParamInfo plain;
            plain.id = "mix";
            plain.minValue = 0.0f;
            plain.maxValue = 100.0f;

            expectEquals (plain.choiceCount(), 0);
            expect (plain.choiceLabel (42.0f).isEmpty());
        }

        beginTest ("a label is found by value, snapped, and refused outside the range");
        {
            pb::ParamInfo info;
            info.minValue = 0.0f;
            info.maxValue = 5.0f;
            info.choices = { "One", "Two", "Three", "Four", "Five", "Six" };

            expectEquals (info.choiceCount(), 6);
            expectEquals (info.choiceLabel (0.0f), juce::String ("One"));
            expectEquals (info.choiceLabel (3.0f), juce::String ("Four"));
            expectEquals (info.choiceLabel (5.0f), juce::String ("Six"));
            expectEquals (info.choiceLabel (2.4f), juce::String ("Three"));      // snapped, not truncated
            expect (info.choiceLabel (6.0f).isEmpty());                          // out of range: no wrong name
            expect (info.choiceLabel (-1.0f).isEmpty());
            expect (info.choiceLabel (std::nanf ("")).isEmpty());
        }

        beginTest ("a param whose values start somewhere other than zero indexes from minValue");
        {
            pb::ParamInfo stages;
            stages.minValue = 1.0f;
            stages.maxValue = 3.0f;
            stages.choices = { "1", "2", "3" };

            expectEquals (stages.choiceLabel (1.0f), juce::String ("1"));
            expectEquals (stages.choiceLabel (3.0f), juce::String ("3"));
        }

        beginTest ("pump's own names arrive in the pedals");
        {
            pb::PumpSpringAdapter spring;
            const auto springParams = spring.params();
            expectEquals (springParams[0].choiceLabel (0.0f), juce::String ("Two-spring tank"));
            expectEquals (springParams[0].choiceLabel (2.0f), juce::String ("Short tank"));

            pb::PumpTubeAdapter tube;
            const auto tubeParams = tube.params();
            expectEquals (tubeParams[0].choiceLabel (1.0f), juce::String ("Triode"));
            expectEquals (tubeParams[13].choiceLabel (2.0f), juce::String ("4x"));      // oversampling

            pb::PumpTwangAdapter twang;
            const auto twangParams = twang.params();
            expectEquals (twangParams[12].choiceLabel ((float) pump::coil::kChordFifth), juce::String ("Fifth"));

            pb::PumpEqAdapter eq;
            const auto eqParams = eq.params();
            expectEquals (eqParams[14].choiceLabel (4.0f), juce::String ("High Cut"));   // band 2's shape (stride 6)
        }

        beginTest ("every labelled param on every pedal has exactly one name per value");
        {
            std::vector<std::unique_ptr<pb::IEffect>> pedals;
            pedals.push_back (std::make_unique<pb::PumpSpringAdapter>());
            pedals.push_back (std::make_unique<pb::PumpEchoAdapter>());
            pedals.push_back (std::make_unique<pb::PumpTubeAdapter>());
            pedals.push_back (std::make_unique<pb::PumpCrushAdapter>());
            pedals.push_back (std::make_unique<pb::PumpSwirlAdapter>());
            pedals.push_back (std::make_unique<pb::PumpCompAdapter>());
            pedals.push_back (std::make_unique<pb::PumpHeatAdapter>());
            pedals.push_back (std::make_unique<pb::PumpToneAdapter>());
            pedals.push_back (std::make_unique<pb::PumpVerbAdapter>());
            pedals.push_back (std::make_unique<pb::PumpLimitAdapter>());
            pedals.push_back (std::make_unique<pb::PumpSnapAdapter>());
            pedals.push_back (std::make_unique<pb::PumpStripAdapter>());
            pedals.push_back (std::make_unique<pb::PumpCalmAdapter>());
            pedals.push_back (std::make_unique<pb::PumpGateAdapter>());
            pedals.push_back (std::make_unique<pb::PumpGrainAdapter>());
            pedals.push_back (std::make_unique<pb::PumpStutterAdapter>());
            pedals.push_back (std::make_unique<pb::PumpChopAdapter>());
            pedals.push_back (std::make_unique<pb::PumpWideAdapter>());
            pedals.push_back (std::make_unique<pb::PumpBandAdapter>());
            pedals.push_back (std::make_unique<pb::PumpEqAdapter>());
            pedals.push_back (std::make_unique<pb::PumpTwangAdapter>());

            int labelled = 0;

            for (auto& pedal : pedals)
            {
                for (const auto& info : pedal->params())
                {
                    if (info.choices.empty())
                        continue;

                    ++labelled;

                    const int expected = (int) std::lround (info.maxValue - info.minValue) + 1;

                    if (info.choiceCount() != expected)
                        logMessage (pedal->typeId() + ": " + info.id + " has " + juce::String (info.choiceCount())
                                    + " names for " + juce::String (expected) + " values");

                    expectEquals (info.choiceCount(), expected);

                    for (const auto& name : info.choices)
                        expect (name.isNotEmpty());

                    // The default is what the editor shows on a fresh pedal; it must name a real choice.
                    expect (info.choiceLabel (info.defaultValue).isNotEmpty());
                }
            }

            logMessage ("labelled params across the pedals: " + juce::String (labelled));
            expect (labelled > 40);             // the point of the card is coverage, not a token param or two
        }
    }
};

static ParamChoicesTests paramChoicesTests;
