#pragma once
// Device-wide settings, as opposed to a board's own state. Right now that is the tempo: the owner's decision is that
// the tempo belongs to the device (the header's knob), not to a board, so it survives a board swap and a relaunch.
//
// The file is a juce::PropertiesFile (XML) in ~/Music/pump BOARD/settings, next to current.board, so it is somewhere a
// musician can find it. Kept free of UI and of the registry so the suite can exercise it against a temporary file.
#include <juce_data_structures/juce_data_structures.h>

namespace pb
{
namespace settings
{
// The app's own settings file. Creates its folder on first use.
juce::File appSettingsFile();

// Read/write against any file, so tests use a temporary one and never touch the user's folder.
float readTempo (const juce::File& file, float fallback = 120.0f);
bool writeTempo (const juce::File& file, float bpm);

// The same pair against appSettingsFile().
float readTempo();
bool writeTempo (float bpm);
} // namespace settings
} // namespace pb
