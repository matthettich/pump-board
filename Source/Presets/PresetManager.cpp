#include "Presets/PresetManager.h"
#include <algorithm>

namespace pb
{
namespace
{
// ~/Music is the user's own music folder on macOS, which is where a musician looks for boards. The app's
// other state (audio.xml, the old board.xml) stays in Application Support, where it belongs.
juce::File presetRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("pump BOARD");
}
} // namespace

juce::File presetBoardsDirectory()
{
    return presetRoot().getChildFile ("Boards");
}

juce::File workingBoardFile()
{
    return presetRoot().getChildFile ("current.board");
}

PresetManager::PresetManager (juce::File directoryIn) : directory (std::move (directoryIn)) {}

PresetManager PresetManager::forApp()
{
    return PresetManager (presetBoardsDirectory());
}

juce::Array<juce::File> PresetManager::list() const
{
    juce::Array<juce::File> files;

    if (directory.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*.board", juce::File::findFiles))
            files.add (entry.getFile());

    // Sorted by name, case-insensitively: this order is the program-change number, so it must not depend on
    // the filesystem's own ordering.
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    // A typed name must never be able to point outside this folder. createLegalFileName drops illegal characters
    // rather than replacing them, so a name of nothing but those became ".board" (hidden, and missing from the
    // program-change list); leading dots would hide a file the same way. Both fall back to a visible name.
    auto legal = juce::File::createLegalFileName (name.trim()).trim();

    while (legal.startsWithChar ('.'))
        legal = legal.substring (1).trimStart();

    if (legal.isEmpty())
        legal = defaultBoardName();

    return directory.getChildFile (legal + ".board");
}

juce::String PresetManager::nameOf (const juce::File& file) const
{
    return file.getFileNameWithoutExtension();
}

bool PresetManager::read (const juce::File& file, BoardData& out, LoadResult& result) const
{
    result = {};

    if (file.getFullPathName().isEmpty() || ! file.existsAsFile())
    {
        result.error = "there is no board file at " + file.getFullPathName();
        return false;
    }

    result = fromJson (file.loadFileAsString(), out);
    return result.ok;
}

bool PresetManager::write (const juce::File& file, const BoardData& data) const
{
    if (file.getFullPathName().isEmpty())
        return false;

    file.getParentDirectory().createDirectory();

    // replaceWithText writes to a temporary file and moves it into place, so a crash mid-save leaves the
    // previous board intact rather than a half-written one.
    return file.replaceWithText (toJson (data));
}

juce::File PresetManager::saveAs (const juce::String& name, const BoardData& data) const
{
    if (name.trim().isEmpty())
        return {};

    const auto file = fileFor (name);

    auto copy = data;
    copy.name = nameOf (file);              // the legal, stored name, so name -> file -> name round-trips

    return write (file, copy) ? file : juce::File();
}

bool PresetManager::rename (const juce::File& file, const juce::String& newName) const
{
    const auto trimmed = newName.trim();

    if (trimmed.isEmpty() || ! file.existsAsFile())
        return false;

    const auto target = fileFor (trimmed);

    if (target == file)
    {
        if (target.getFileName() == file.getFileName())
            return true;                              // the very same name: nothing to do, and not a failure

        // Case-only change ("clean" -> "Clean"). On a case-insensitive disk (macOS) the two paths are the same
        // file, so writing the new one and trashing the old would trash the result. Go through a temporary name.
        const auto temp = directory.getNonexistentChildFile (".pb-rename", ".tmp", false);

        if (! file.moveFileTo (temp))
            return false;

        BoardData data;
        LoadResult result;

        if (read (temp, data, result))
        {
            data.name = nameOf (target);

            if (write (target, data))
            {
                temp.deleteFile();                    // a scratch copy of what is now in `target`
                return true;
            }
        }

        temp.moveFileTo (file);                       // put the board back where it was
        return false;
    }

    if (target.existsAsFile())
        return false;                                 // never overwrite another board by accident

    BoardData data;
    LoadResult result;

    if (! read (file, data, result))
        return false;

    const auto written = saveAs (trimmed, data);

    if (written.getFullPathName().isEmpty())
        return false;

    if (! trash (file))                               // recoverable, unlike deleteFile
    {
        written.deleteFile();                         // the Bin refused: undo, rather than leave two copies
        return false;
    }

    return true;
}

bool PresetManager::remove (const juce::File& file) const
{
    if (! file.existsAsFile())
        return false;

    return trash (file);
}
} // namespace pb
