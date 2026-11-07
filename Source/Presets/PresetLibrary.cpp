#include "Presets/PresetLibrary.h"
#include <algorithm>

namespace pb
{
juce::File presetsDirectory()
{
    return boardFile().getParentDirectory().getChildFile ("presets");
}

PresetLibrary::PresetLibrary (juce::File directoryIn) : directory (std::move (directoryIn)) {}

PresetLibrary PresetLibrary::forApp()
{
    return PresetLibrary (presetsDirectory());
}

juce::Array<juce::File> PresetLibrary::list() const
{
    juce::Array<juce::File> files;

    if (directory.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*.xml", juce::File::findFiles))
            files.add (entry.getFile());

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

juce::File PresetLibrary::fileFor (const juce::String& name) const
{
    // A name typed by a person must never be able to point outside this folder.
    return directory.getChildFile (juce::File::createLegalFileName (name.trim()) + ".xml");
}

bool PresetLibrary::save (const juce::String& name, const BoardModel& model, const ParameterRegistry& registry) const
{
    if (name.trim().isEmpty())
        return false;

    return saveBoard (fileFor (name), model, registry);
}
} // namespace pb
