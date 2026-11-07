#pragma once
// Presets, second half: several named boards in one folder, listed and loaded by name.
#include "Presets/BoardPreset.h"

namespace pb
{
// ~/Library/Application Support/pump BOARD/presets
juce::File presetsDirectory();

class PresetLibrary
{
public:
    explicit PresetLibrary (juce::File directory);

    static PresetLibrary forApp();

    juce::Array<juce::File> list() const;                 // sorted by name, "*.xml" only
    juce::File fileFor (const juce::String& name) const;  // legal file name in this folder, ".xml" appended
    bool save (const juce::String& name, const BoardModel&, const ParameterRegistry&) const;

private:
    juce::File directory;
};
} // namespace pb
