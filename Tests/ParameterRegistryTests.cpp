#include <juce_core/juce_core.h>
#include "Control/ParameterRegistry.h"

class ParameterRegistryTests final : public juce::UnitTest
{
public:
    ParameterRegistryTests() : juce::UnitTest ("ParameterRegistry", "control") {}

    static pb::ParameterRegistry::Entry entry (const juce::String& id, float lo, float hi, float def, bool toggle = false)
    {
        pb::ParameterRegistry::Entry e;
        e.id = id;
        e.displayName = id;
        e.minValue = lo;
        e.maxValue = hi;
        e.defaultValue = def;
        e.isToggle = toggle;
        return e;
    }

    void runTest() override
    {
        using pb::ParameterRegistry;

        beginTest ("add is idempotent by id");
        {
            ParameterRegistry registry;
            const int first = registry.add (entry ("board/inputTrim", -24.0f, 12.0f, 0.0f));
            const int second = registry.add (entry ("board/inputTrim", -24.0f, 12.0f, 0.0f));

            expect (first != ParameterRegistry::kInvalid);
            expect (first == second);
            expectEquals (registry.find ("board/inputTrim"), first);
            expectEquals (registry.get (first), 0.0f);
            expect (registry.info (first).inUse);
        }

        beginTest ("set clamps to the entry's range");
        {
            ParameterRegistry registry;
            const int index = registry.add (entry ("board/outputTrim", -24.0f, 12.0f, 0.0f));

            registry.set (index, 100.0f);
            expectEquals (registry.get (index), 12.0f);

            registry.set (index, -100.0f);
            expectEquals (registry.get (index), -24.0f);

            registry.set (index, 3.5f);
            expectEquals (registry.get (index), 3.5f);

            registry.set (index, std::numeric_limits<float>::quiet_NaN());
            expectEquals (registry.get (index), 3.5f);       // rejected, not clamped to an end
        }

        beginTest ("normalised round-trips within 1e-6");
        {
            ParameterRegistry registry;
            const int index = registry.add (entry ("slot/a/mix", 0.0f, 100.0f, 30.0f));

            for (const float value01 : { 0.0f, 0.25f, 0.5f, 1.0f })
            {
                registry.setNormalised (index, value01);
                expectWithinAbsoluteError (registry.getNormalised (index), value01, 1.0e-6f);
            }

            // and reads back the entry's own value, not a stored normalised copy
            registry.set (index, 30.0f);
            expectWithinAbsoluteError (registry.getNormalised (index), 0.30f, 1.0e-6f);
        }

        beginTest ("toggle flips a toggle between 0 and 1");
        {
            ParameterRegistry registry;
            const int index = registry.add (entry ("slot/a/on", 0.0f, 1.0f, 0.0f, true));

            registry.toggle (index);
            expectEquals (registry.get (index), 1.0f);

            registry.toggle (index);
            expectEquals (registry.get (index), 0.0f);
        }

        beginTest ("remove frees the index for reuse");
        {
            ParameterRegistry registry;
            const int first = registry.add (entry ("slot/a/mix", 0.0f, 100.0f, 30.0f));

            expectEquals (registry.find ("slot/a/mix"), first);

            registry.remove (first);
            expectEquals (registry.find ("slot/a/mix"), ParameterRegistry::kInvalid);
            expect (! registry.info (first).inUse);

            const int reused = registry.add (entry ("slot/b/mix", 0.0f, 100.0f, 30.0f));
            expectEquals (reused, first);
        }

        beginTest ("unknown and invalid indices are harmless");
        {
            ParameterRegistry registry;
            expectEquals (registry.find ("nothing/here"), ParameterRegistry::kInvalid);

            registry.set (ParameterRegistry::kInvalid, 1.0f);
            expectEquals (registry.getNormalised (ParameterRegistry::kInvalid), 0.0f);
            expect (! registry.info (ParameterRegistry::kInvalid).inUse);

            const int index = registry.add (entry ("board/bypass", 0.0f, 1.0f, 0.0f, true));
            const auto before = (int) registry.changeCounter();
            registry.set (index, 1.0f);
            expectEquals ((int) registry.changeCounter(), before + 1);
        }
    }
};

static ParameterRegistryTests parameterRegistryTests;
