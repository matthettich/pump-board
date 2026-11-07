#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace
{
class SmokeTest final : public juce::UnitTest
{
public:
    SmokeTest() : juce::UnitTest ("SmokeTest", "core") {}

    void runTest() override
    {
        beginTest ("trivial arithmetic");
        expect (1 + 1 == 2);

        beginTest ("juce core is linked");
        expect (juce::String ("pump").isNotEmpty());
    }
};

SmokeTest smokeTest;
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int totalFailures = 0;
    int totalPasses = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (auto* result = runner.getResult (i))
        {
            totalFailures += result->failures;
            totalPasses += result->passes;
        }
    }

    std::cout << "\n" << totalPasses << " passed, " << totalFailures << " failed" << std::endl;
    return totalFailures == 0 ? 0 : 1;
}
