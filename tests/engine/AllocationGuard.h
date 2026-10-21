#pragma once

// Copied from eq1's tests/engine/AllocationGuard.h (eq1 at dbb1127). plugkit doesn't hold it, so this copy is
// comp1's against ADR 0006 until it moves there.

#include <cstddef>

namespace comp1::test
{

// Counts heap allocations made on the current thread while it is alive, and the bytes they hold:
// what the allocator gave out, less what the thread freed meanwhile.
class AllocationGuard
{
public:
    AllocationGuard();
    ~AllocationGuard();

    std::size_t allocations() const;
    std::size_t bytesHeld() const;
};

} // namespace comp1::test
