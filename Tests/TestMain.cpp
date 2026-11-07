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

// `PumpBoardTests` runs everything; `PumpBoardTests WideNull` runs only the suites whose name contains "WideNull"
// (handy under valgrind, or to chase one failure).
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
    {
        const juce::String filter (argv[1]);
        juce::Array<juce::UnitTest*> chosen;

        for (auto* test : juce::UnitTest::getAllTests())
            if (test->getName().containsIgnoreCase (filter))
                chosen.add (test);

        runner.runTests (chosen);
    }
    else
    {
        runner.runAllTests();
    }

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
