#include <juce_core/juce_core.h>
#include "Effects/EffectRegistry.h"

namespace
{
class DummyEffect final : public pb::IEffect
{
public:
    explicit DummyEffect (juce::String id, juce::String name) : typeIdString (std::move (id)), nameString (std::move (name)) {}

    juce::String typeId() const override { return typeIdString; }
    juce::String displayName() const override { return nameString; }
    std::vector<pb::ParamInfo> params() const override { return { { "mix", "Mix", 0.0f, 100.0f, 50.0f, "%" } }; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>&) noexcept override {}

private:
    juce::String typeIdString, nameString;
};

pb::EffectDescriptor descriptorFor (const juce::String& typeId, const juce::String& name, const juce::String& category)
{
    pb::EffectDescriptor d;
    d.typeId = typeId;
    d.displayName = name;
    d.category = category;
    d.create = [typeId, name] { return std::make_unique<DummyEffect> (typeId, name); };
    return d;
}
} // namespace

class EffectRegistryTests final : public juce::UnitTest
{
public:
    EffectRegistryTests() : juce::UnitTest ("EffectRegistry", "effects") {}

    void runTest() override
    {
        beginTest ("add then create returns the right effect");
        {
            pb::EffectRegistry registry;
            registry.add (descriptorFor ("pump.spring", "pump SPRING", "space"));

            expect (registry.contains ("pump.spring"));

            auto effect = registry.create ("pump.spring");
            expect (effect != nullptr);

            if (effect != nullptr)
            {
                expectEquals (effect->typeId(), juce::String ("pump.spring"));
                expectEquals (effect->displayName(), juce::String ("pump SPRING"));
                expectEquals ((int) effect->params().size(), 1);
            }
        }

        beginTest ("an unknown typeId returns nullptr");
        {
            pb::EffectRegistry registry;
            expect (registry.create ("pump.nothing") == nullptr);
            expect (! registry.contains ("pump.nothing"));
        }

        beginTest ("list is sorted by category, then display name");
        {
            pb::EffectRegistry registry;
            registry.add (descriptorFor ("pump.echo",   "pump ECHO",   "time"));
            registry.add (descriptorFor ("pump.spring", "pump SPRING", "space"));
            registry.add (descriptorFor ("pump.eq",     "pump EQ",     "filter"));
            registry.add (descriptorFor ("pump.tube",   "pump TUBE",   "drive"));
            registry.add (descriptorFor ("pump.verb",   "pump VERB",   "space"));
            registry.add (descriptorFor ("pump.com",    "pump COMP",   "dynamics"));

            const auto list = registry.list();
            expectEquals ((int) list.size(), 6);

            juce::StringArray ids;
            for (const auto& d : list)
                ids.add (d.typeId);

            // categories in alphabetical order: drive, dynamics, filter, space (two, by name), time
            expectEquals (ids.joinIntoString (" "),
                          juce::String ("pump.tube pump.com pump.eq pump.spring pump.verb pump.echo"));
        }

        beginTest ("a duplicate typeId does not create a second entry");
        {
            pb::EffectRegistry registry;
            registry.add (descriptorFor ("pump.spring", "pump SPRING", "space"));
            registry.add (descriptorFor ("pump.spring", "pump SPRING AGAIN", "other"));

            expect (registry.contains ("pump.spring"));
            expectEquals ((int) registry.list().size(), 1);
        }
    }
};

static EffectRegistryTests effectRegistryTests;
