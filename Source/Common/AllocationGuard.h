#pragma once
// Card 2.4: prove the audio thread does not allocate. In Debug builds (`PB_ALLOCATION_GUARD`) the global
// allocation hooks in the .cpp count every allocation made while a Scope is alive; `processBlock` opens one.
// In Release nothing is replaced and the Scope is a no-op, so the kit rule costs nothing where it ships.
#include <cstddef>

namespace pb::alloc
{
#ifdef PB_ALLOCATION_GUARD
int violations() noexcept;
void resetViolations() noexcept;

// RAII: allocations inside this scope are counted against the caller. Do not allocate to construct it.
struct Scope
{
    Scope() noexcept;
    ~Scope() noexcept;

    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;
};
#else
inline int violations() noexcept { return 0; }
inline void resetViolations() noexcept {}

struct Scope
{
    Scope() noexcept = default;
};
#endif
} // namespace pb::alloc
