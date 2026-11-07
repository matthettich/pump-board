#pragma once
// Open item 1 (6 Nov review): adapters must raise `dirty` only when a pushed value actually differs, because
// Slot::process re-pushes every parameter every block and pump reconfigures its engine on setSettings().
// Comparison is exact on purpose: the same registry value converts to the same field value every block, and an
// exact compare keeps every genuine edit (however small) and the null tests bit-identical. Written with < / >
// rather than == so -Wfloat-equal stays quiet.
#include <type_traits>

namespace pb
{
// Store `value` into `field`; set `dirty` only if the stored value changed. Returns whether it changed.
template <class Field, class Value>
inline bool assignField (Field& field, const Value& value, bool& dirty) noexcept
{
    const Field converted = static_cast<Field> (value);

    if (! (field < converted) && ! (converted < field))
        return false;

    field = converted;
    dirty = true;
    return true;
}
} // namespace pb
