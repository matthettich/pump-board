#pragma once
// Card 4.3: named boards on disk. The folder is the preset list; the order of its sorted names is the
// program-change number. Files are the card 4.1 format (.board), so slots, macros and mappings all travel.
#include "Presets/BoardSerializer.h"
#include <juce_core/juce_core.h>
#include <functional>

namespace pb
{
// ~/Music/pump BOARD/Boards — where the named boards live.
juce::File presetBoardsDirectory();

// The board the app was last on, beside that folder rather than inside it: a working copy that must not
// appear in the preset list (it would take a program number that is not a preset).
juce::File workingBoardFile();

// A working board that could not be read (damaged, or saved by a newer build) is moved aside to
// "<name>.unreadable" (numbered if that is taken) so the next autosave does not destroy it. Returns the kept file, or an
// empty File when there was nothing to keep or it could not be moved.
juce::File setAsideUnreadable (const juce::File& file);

class PresetManager
{
public:
    explicit PresetManager (juce::File directory);

    static PresetManager forApp();

    juce::Array<juce::File> list() const;                    // "*.board", sorted by name
    juce::File fileFor (const juce::String& name) const;      // a legal name in this folder, ".board" appended
    juce::String nameOf (const juce::File& file) const;

    // Reads a board file. Returns false with `result.error` set when the file is missing or not a board.
    bool read (const juce::File&, BoardData& out, LoadResult& result) const;
    bool write (const juce::File&, const BoardData&) const;

    // Writes `data` under `name`, stamping the file's own name into it. Empty File when it cannot.
    juce::File saveAs (const juce::String& name, const BoardData& data) const;

    // Card 5.26: housekeeping that used to mean going to Finder. Rename writes the new file (with the new name
    // stamped inside it) and trashes the old one; delete trashes the file. Neither guesses at a name.
    bool rename (const juce::File& file, const juce::String& newName) const;
    bool remove (const juce::File& file) const;

    // The Bin. Defaults to File::moveToTrash; a test swaps in one that fails to prove rename does not leave
    // two copies behind (open item 7).
    using TrashFunction = std::function<bool (const juce::File&)>;
    void setTrashFunction (TrashFunction f) { trashFunction = std::move (f); }

    // What a name made only of characters a file cannot hold becomes (open item 7).
    static juce::String defaultBoardName() { return "Untitled"; }

private:
    bool trash (const juce::File& file) const { return trashFunction ? trashFunction (file) : file.moveToTrash(); }

    juce::File directory;
    TrashFunction trashFunction;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
} // namespace pb
