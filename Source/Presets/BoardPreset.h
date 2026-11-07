#pragma once
// Presets, first half: the board as a file. Every slot, which pedal sits in it, its switch and every value in
// pump's own units. Named preset slots (several boards, browsed) come on top of this format.
#include "Control/ParameterRegistry.h"
#include "Engine/BoardModel.h"
#include <juce_core/juce_core.h>

namespace pb
{
// The pedal board as it was left, in the app's own support folder.
juce::File boardFile();

// Caller owns the returned element.
juce::XmlElement* boardToXml (const BoardModel&, const ParameterRegistry&);

// Clears the model and rebuilds it in file order. Returns false if the XML is not a board.
// A slot whose pedal is no longer registered is skipped rather than failing the whole board.
bool boardFromXml (const juce::XmlElement&, BoardModel&, ParameterRegistry&);

bool saveBoard (const juce::File&, const BoardModel&, const ParameterRegistry&);
bool loadBoard (const juce::File&, BoardModel&, ParameterRegistry&);   // false when the file is absent or unreadable
} // namespace pb
