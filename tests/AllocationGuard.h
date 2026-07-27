#pragma once

#include <cstddef>

// Shared instrumentation for the audio-thread allocation regression tests
// (tests/RobustnessTests.cpp). A single translation unit
// (AllocationGuard.cpp) globally replaces operator new/delete to count
// allocations while a guard is active; every other test file only needs
// this declaration to construct an AllocationGuard and read its count().
//
// Ported unchanged (bar these comments and the namespace note) from sibling
// plugin aureate's tests/AllocationGuard.h, which in turn follows overture's -
// the suite-wide pattern. Triptych had no allocation-counting mechanism before
// v0.5.0: pluginval and auval do not do allocation-instrumented profiling, and
// none of the pre-existing Catch2 tests could observe a heap allocation on the
// audio thread. v0.5.0 adds delay lines, key buffers and a sidechain crossover
// pair to process(), so the gate is worth having.
namespace TestAlloc
{
    // Number of operator-new calls observed while any AllocationGuard is
    // alive, since the most recently constructed guard reset it to zero.
    // Not meaningful outside a guard's lifetime.
    std::size_t currentAllocationCount() noexcept;

    // RAII scope: resets the shared allocation counter to zero on
    // construction and stops counting on destruction. Guards are not
    // reentrant/nestable - only construct one at a time (which is all these
    // tests need).
    class AllocationGuard
    {
    public:
        AllocationGuard() noexcept;
        ~AllocationGuard() noexcept;

        AllocationGuard (const AllocationGuard&) = delete;
        AllocationGuard& operator= (const AllocationGuard&) = delete;

        // Allocations counted by the global operator new override since
        // this guard was constructed.
        std::size_t count() const noexcept { return currentAllocationCount(); }
    };
}
