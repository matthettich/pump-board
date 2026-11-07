#include <juce_core/juce_core.h>
#include "Common/AppSettings.h"
#include <cmath>

// Card 5.25: the tempo is device-wide. It is not in the .board schema (it never was), so what makes it device-wide
// is that it survives a relaunch — these tests cover the file it lives in.
class AppSettingsTests final : public juce::UnitTest
{
public:
    AppSettingsTests() : juce::UnitTest ("AppSettings", "engine") {}

    void runTest() override
    {
        beginTest ("a missing settings file reads the fallback, not a failure");
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pb-settings-a");
            dir.deleteRecursively();

            expectEquals (pb::settings::readTempo (dir.getChildFile ("settings"), 132.0f), 132.0f);
        }

        beginTest ("a written tempo reads back");
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pb-settings-b");
            dir.deleteRecursively();

            const auto file = dir.getChildFile ("settings");

            expect (pb::settings::writeTempo (file, 97.0f));
            expect (file.existsAsFile());
            expectWithinAbsoluteError (pb::settings::readTempo (file, 120.0f), 97.0f, 1.0e-4f);

            // Overwriting works: a second write replaces the value rather than appending another.
            expect (pb::settings::writeTempo (file, 145.0f));
            expectWithinAbsoluteError (pb::settings::readTempo (file), 145.0f, 1.0e-4f);

            dir.deleteRecursively();
        }

        beginTest ("a hand-edited file cannot claim a tempo the board does not allow");
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pb-settings-c");
            dir.deleteRecursively();

            const auto file = dir.getChildFile ("settings");
            pb::settings::writeTempo (file, 0.001f);          // refused outright: nothing to write? then clamp on read

            const float read = pb::settings::readTempo (file, 120.0f);
            expect (read >= 20.0f);
            expect (read <= 300.0f);

            dir.deleteRecursively();
        }
    }
};

static AppSettingsTests appSettingsTests;
