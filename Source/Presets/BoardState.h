#pragma once
// Card 4.2: the live board <-> BoardData. capture() reads the model, the macro bank and the mapping list into
// the plain data a .board file holds; apply() rebuilds all three from it. Message thread only.
//
// Slot references are by position, never by uid: uids are "s1, s2, ..." handed out by BoardModel and change
// every launch, so a file that stored them would lose every mapping the next time the app started.
#include "Control/LearnController.h"
#include "Control/MacroBank.h"
#include "Engine/BoardModel.h"
#include "Presets/BoardSerializer.h"

namespace pb
{
// The board's name is not board state (the preset manager owns it), so capture leaves BoardData::name empty.
BoardData capture (const BoardModel&, const MacroBank&, const LearnController&, const ParameterRegistry&);

// Clears the board, then rebuilds it: slots, macros, mappings. An unknown effect id leaves an empty position;
// a mapping or a macro target that points at a pedal the loaded board does not have is dropped. Both cases
// add a warning rather than failing the load, and ok is true whenever the file itself was readable.
LoadResult apply (const BoardData&, BoardModel&, MacroBank&, LearnController&, ParameterRegistry&);
} // namespace pb
