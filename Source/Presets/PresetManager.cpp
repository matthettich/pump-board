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
    // A typed name must never be able to point outside this folder.
    return directory.getChildFile (juce::File::createLegalFileName (name.trim()) + ".board");
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
        return true;                                  // nothing to do, and not a failure

    if (target.existsAsFile())
        return false;                                 // never overwrite another board by accident

    BoardData data;
    LoadResult result;

    if (! read (file, data, result))
        return false;

    if (saveAs (trimmed, data).getFullPathName().isEmpty())
        return false;

    file.moveToTrash();                               // recoverable, unlike deleteFile
    return true;
}

bool PresetManager::remove (const juce::File& file) const
{
    if (! file.existsAsFile())
        return false;

    return file.moveToTrash();
}
} // namespace pb
