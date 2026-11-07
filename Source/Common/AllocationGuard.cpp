#include "Common/AllocationGuard.h"

#ifdef PB_ALLOCATION_GUARD

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
thread_local int depth = 0;
std::atomic<int> violationCount { 0 };

void noteIfGuarded() noexcept
{
    if (depth > 0)
        violationCount.fetch_add (1, std::memory_order_relaxed);
}
} // namespace

namespace pb::alloc
{
int violations() noexcept { return violationCount.load (std::memory_order_relaxed); }
void resetViolations() noexcept { violationCount.store (0, std::memory_order_relaxed); }

Scope::Scope() noexcept { ++depth; }
Scope::~Scope() noexcept { --depth; }
} // namespace pb::alloc

// ---- the global allocation hooks ------------------------------------------------------------------------
// Debug only. Each one counts when a Scope is open and then does exactly what the default allocator would
// have done, so behaviour is unchanged apart from the count.
void* operator new (std::size_t size)
{
    noteIfGuarded();

    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return ::operator new (size);
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

// Aligned forms (C++17). posix_memalign is the macOS route; the app is Mac-only, and the aligned hooks are
// only reached in Debug.
void* operator new (std::size_t size, std::align_val_t alignment)
{
    noteIfGuarded();

    void* p = nullptr;

    if (posix_memalign (&p, (std::size_t) alignment, size != 0 ? size : 1) == 0)
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return ::operator new (size, alignment);
}

void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

#endif // PB_ALLOCATION_GUARD
