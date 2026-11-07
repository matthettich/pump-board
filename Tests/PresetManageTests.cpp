#include <juce_core/juce_core.h>
#include "Presets/PresetManager.h"
#include <cmath>

// Card 5.26: renaming and deleting a saved board from inside the app. Both work on real files in a temporary folder,
// so the tests never touch ~/Music.
class PresetManageTests final : public juce::UnitTest
{
public:
    PresetManageTests() : juce::UnitTest ("PresetManage", "presets") {}

    static juce::File tempDir (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("pb-presets-" + name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    static pb::BoardData board (const juce::String& name)
    {
        pb::BoardData data;
        data.name = name;
        data.inputTrimDb = -3.0f;
        return data;
    }

    void runTest() override
    {
        beginTest ("rename writes the new file with the new name inside it, and removes the old one");
        {
            const auto dir = tempDir ("rename");
            const pb::PresetManager manager (dir);

            const auto first = manager.saveAs ("Clean", board ("Clean"));
            expect (first.existsAsFile());

            expect (manager.rename (first, "Dirty"));
            expect (! first.existsAsFile());

            const auto files = manager.list();
            expectEquals (files.size(), 1);
            expectEquals (manager.nameOf (files[0]), juce::String ("Dirty"));

            pb::BoardData loaded;
            pb::LoadResult result;
            expect (manager.read (files[0], loaded, result));
            expectEquals (loaded.name, juce::String ("Dirty"));      // the name stamped inside follows the file
            expectWithinAbsoluteError (loaded.inputTrimDb, -3.0f, 1.0e-4f);

            dir.deleteRecursively();
        }

        beginTest ("rename refuses a name already taken rather than overwriting a board");
        {
            const auto dir = tempDir ("collide");
            const pb::PresetManager manager (dir);

            const auto a = manager.saveAs ("One", board ("One"));
            const auto b = manager.saveAs ("Two", board ("Two"));

            expect (! manager.rename (a, "Two"));
            expect (a.existsAsFile());
            expect (b.existsAsFile());
            expectEquals (manager.list().size(), 2);

            dir.deleteRecursively();
        }

        beginTest ("rename refuses an empty name and a missing file");
        {
            const auto dir = tempDir ("empty");
            const pb::PresetManager manager (dir);
            const auto a = manager.saveAs ("Kept", board ("Kept"));

            expect (! manager.rename (a, "   "));
            expect (! manager.rename (dir.getChildFile ("nope.board"), "Anything"));
            expect (a.existsAsFile());

            dir.deleteRecursively();
        }

        beginTest ("delete removes exactly one board");
        {
            const auto dir = tempDir ("delete");
            const pb::PresetManager manager (dir);

            const auto a = manager.saveAs ("Keep", board ("Keep"));
            const auto b = manager.saveAs ("Bin", board ("Bin"));

            expect (manager.remove (b));
            expect (! manager.remove (b));               // already gone: a false, not a crash

            const auto files = manager.list();
            expectEquals (files.size(), 1);
            expectEquals (manager.nameOf (files[0]), juce::String ("Keep"));
            expect (a.existsAsFile());

            dir.deleteRecursively();
        }
    }
};

static PresetManageTests presetManageTests;
