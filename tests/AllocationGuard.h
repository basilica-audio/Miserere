#pragma once

#include <atomic>
#include <cstdlib>
#include <new>

// A scoped heap-allocation counter for the audio thread's real-time-safety
// contract (docs/architecture.md's "no allocation once prepare() has
// completed"). Overrides the process-wide global operator new/delete to
// count allocations while an AllocationGuard::Scope is alive, so a test can
// assert that a processBlock() call (or any other audio-thread path)
// performs zero heap operations.
//
// NOTE: the briefing referenced an existing "AllocationGuard" pattern in
// sibling repo lancet/tests as a model to follow; on inspection at the time
// of writing, lancet's test suite does not contain such a harness (nor does
// any other sibling repo). This is a fresh implementation of the standard
// global-operator-new-override idiom, not a copy of prior art - flagging
// this explicitly per the "stubs/deviations must be flagged" convention.
//
// Usage:
//
//   AllocationGuard::reset();
//   { AllocationGuard::Scope scope; engine.process (block); }
//   CHECK (AllocationGuard::allocationCount() == 0);
//
// Thread-safety: the counter is a single process-wide atomic, adequate for
// the Tests binary's single-threaded Catch2 execution model. The override
// is unconditional (not scoped to "only while a Scope is active") because
// operator new/delete cannot be swapped in and out at runtime; instead
// every allocation increments the counter, and callers reset() the counter
// immediately before the region under test.
namespace AllocationGuard
{
    inline std::atomic<std::size_t>& counter() noexcept
    {
        static std::atomic<std::size_t> value { 0 };
        return value;
    }

    inline void reset() noexcept { counter().store (0, std::memory_order_relaxed); }
    inline std::size_t allocationCount() noexcept { return counter().load (std::memory_order_relaxed); }

    // RAII marker retained for call-site clarity (see class comment); the
    // counting itself is always active process-wide once this translation
    // unit is linked in, so the Scope's constructor/destructor are
    // deliberately trivial.
    struct Scope
    {
        Scope() = default;
        ~Scope() = default;
    };
}

// NOTE: global replacement operator new/delete must not be declared `inline`
// (the standard forbids it for replacement functions) - safe here because
// this header is deliberately included by exactly one translation unit
// (AllocationGuardTests.cpp), so there is no ODR concern despite the
// definitions living in a header.
void* operator new (std::size_t size)
{
    AllocationGuard::counter().fetch_add (1, std::memory_order_relaxed);
    if (auto* ptr = std::malloc (size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete (void* ptr) noexcept { std::free (ptr); }
void operator delete (void* ptr, std::size_t) noexcept { std::free (ptr); }

void* operator new[] (std::size_t size)
{
    AllocationGuard::counter().fetch_add (1, std::memory_order_relaxed);
    if (auto* ptr = std::malloc (size == 0 ? 1 : size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete[] (void* ptr) noexcept { std::free (ptr); }
void operator delete[] (void* ptr, std::size_t) noexcept { std::free (ptr); }
