#include <juce_core/juce_core.h>
#include <cmath>
#include "Control/MacroBank.h"
#include "Control/ParameterRegistry.h"

namespace
{
int addParam (pb::ParameterRegistry& registry, const juce::String& id, float lo, float hi, float def = 0.0f)
{
    pb::ParameterRegistry::Entry entry;
    entry.id = id;
    entry.displayName = id;
    entry.minValue = lo;
    entry.maxValue = hi;
    entry.defaultValue = def;
    return registry.add (entry);
}

pb::MacroBank::Target targetFor (int index, float lo, float hi, pb::MacroBank::Curve curve)
{
    pb::MacroBank::Target target;
    target.paramIndex = index;
    target.minValue = lo;
    target.maxValue = hi;
    target.curve = curve;
    return target;
}
} // namespace

class MacroBankTests final : public juce::UnitTest
{
public:
    MacroBankTests() : juce::UnitTest ("MacroBank", "control") {}

    void runTest() override
    {
        beginTest ("the four curves shape 0, 0.5 and 1 the way the card says");
        {
            pb::ParameterRegistry registry;
            const int index = addParam (registry, "test/curve", 0.0f, 1.0f);

            auto at = [] (pb::MacroBank::Curve curve, int paramIndex, float lo, float hi, float macro)
            {
                return pb::MacroBank::mapValue (macro, targetFor (paramIndex, lo, hi, curve));
            };

            // A range that is not 0..1, so the mapping into the target's units is proved too.
            const float lo = 10.0f, hi = 20.0f;

            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Linear, index, lo, hi, 0.0f), 10.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Linear, index, lo, hi, 0.5f), 15.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Linear, index, lo, hi, 1.0f), 20.0f, 1.0e-6f);

            // Exponential is v squared, so halfway is a quarter of the way up.
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Exponential, index, lo, hi, 0.0f), 10.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Exponential, index, lo, hi, 0.5f), 12.5f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Exponential, index, lo, hi, 1.0f), 20.0f, 1.0e-6f);

            // Logarithmic is the square root: halfway is about 0.707 of the way up.
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Logarithmic, index, lo, hi, 0.0f), 10.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Logarithmic, index, lo, hi, 0.5f), 10.0f + std::sqrt (0.5f) * 10.0f, 1.0e-5f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Logarithmic, index, lo, hi, 1.0f), 20.0f, 1.0e-6f);

            // Inverted runs the other way.
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Inverted, index, lo, hi, 0.0f), 20.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Inverted, index, lo, hi, 0.5f), 15.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Inverted, index, lo, hi, 1.0f), 10.0f, 1.0e-6f);

            // Out-of-range input is clamped, not extrapolated.
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Linear, index, lo, hi, -1.0f), 10.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Linear, index, lo, hi, 2.0f), 20.0f, 1.0e-6f);
            expectWithinAbsoluteError (at (pb::MacroBank::Curve::Inverted, index, lo, hi, 2.0f), 10.0f, 1.0e-6f);
        }

        beginTest ("eight macros are registry params from the start, so MIDI can reach them");
        {
            pb::ParameterRegistry registry;
            pb::MacroBank bank (registry);

            for (int i = 0; i < pb::MacroBank::kNumMacros; ++i)
            {
                const int index = bank.macroParamIndex (i);
                expect (index != pb::ParameterRegistry::kInvalid);
                expectEquals (registry.find ("macro/" + juce::String (i)), index);
                expectWithinAbsoluteError (registry.get (index), 0.0f, 1.0e-6f);
                expectEquals (registry.info (index).minValue, 0.0f);
                expectEquals (registry.info (index).maxValue, 1.0f);
            }

            expectEquals (bank.getName (0), juce::String ("MACRO 1"));
        }

        beginTest ("a macro writes every one of its targets, each through its own curve");
        {
            pb::ParameterRegistry registry;
            pb::MacroBank bank (registry);

            const int direct = addParam (registry, "test/direct", 0.0f, 100.0f);
            const int curved = addParam (registry, "test/curved", 0.0f, 100.0f);

            bank.setTargets (2, { targetFor (direct, 0.0f, 100.0f, pb::MacroBank::Curve::Linear),
                                  targetFor (curved, 0.0f, 100.0f, pb::MacroBank::Curve::Exponential) });

            expectEquals ((int) bank.getTargets (2).size(), 2);

            registry.set (bank.macroParamIndex (2), 0.5f);
            bank.update();

            expectWithinAbsoluteError (registry.get (direct), 50.0f, 1.0e-4f);
            expectWithinAbsoluteError (registry.get (curved), 25.0f, 1.0e-4f);

            registry.set (bank.macroParamIndex (2), 1.0f);
            bank.update();

            expectWithinAbsoluteError (registry.get (direct), 100.0f, 1.0e-4f);
            expectWithinAbsoluteError (registry.get (curved), 100.0f, 1.0e-4f);

            // A macro that did not move leaves its targets alone, even after one was set by hand.
            registry.set (direct, 7.0f);
            bank.update();
            expectWithinAbsoluteError (registry.get (direct), 7.0f, 1.0e-6f);

            // Another macro's knob must not disturb this one.
            registry.set (bank.macroParamIndex (3), 1.0f);
            bank.update();
            expectWithinAbsoluteError (registry.get (direct), 7.0f, 1.0e-6f);
        }

        beginTest ("adding a target lands it where the macro already is");
        {
            pb::ParameterRegistry registry;
            pb::MacroBank bank (registry);
            const int index = addParam (registry, "test/late", 0.0f, 100.0f);

            registry.set (bank.macroParamIndex (0), 0.25f);
            bank.update();

            bank.setTargets (0, { targetFor (index, 0.0f, 100.0f, pb::MacroBank::Curve::Linear) });

            expectWithinAbsoluteError (registry.get (index), 25.0f, 1.0e-4f);

            // And a target whose index is not a real param is skipped, not written.
            bank.setTargets (1, { targetFor (pb::ParameterRegistry::kInvalid, 0.0f, 1.0f,
                                             pb::MacroBank::Curve::Linear) });
            registry.set (bank.macroParamIndex (1), 1.0f);
            bank.update();
            expectEquals ((int) bank.getTargets (1).size(), 1);
        }

        beginTest ("names are editable, trimmed, and fall back to the macro's number");
        {
            pb::ParameterRegistry registry;
            pb::MacroBank bank (registry);

            bank.setName (4, "  SPACE  ");
            expectEquals (bank.getName (4), juce::String ("SPACE"));

            bank.setName (4, "   ");
            expectEquals (bank.getName (4), juce::String ("MACRO 5"));

            expectEquals (bank.getName (8), juce::String());
            expect (bank.getTargets (-1).empty());
            bank.setName (99, "nope");
            bank.setTargets (99, {});
            expect (bank.getTargets (99).empty());
        }
    }
};

static MacroBankTests macroBankTests;
