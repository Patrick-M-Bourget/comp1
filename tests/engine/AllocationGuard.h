#pragma once

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
