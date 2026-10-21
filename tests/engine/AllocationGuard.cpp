#include "AllocationGuard.h"

#include <cstdlib>
#if defined(_WIN32)
    #include <malloc.h>
#elif defined(__APPLE__)
    #include <malloc/malloc.h>
#else
    #include <malloc.h>
#endif
#include <new>

namespace
{
thread_local bool guarding = false;
thread_local std::size_t count = 0;
thread_local std::size_t held = 0;

// The bytes a block really takes in the heap (the allocator rounds a request up), so that what an
// allocation adds and what its free takes away agree.
std::size_t blockSize (void* p) noexcept
{
#if defined(_WIN32)
    return _msize (p);
#elif defined(__APPLE__)
    return malloc_size (p);
#else
    return malloc_usable_size (p);
#endif
}
std::size_t alignedBlockSize (void* p, std::size_t align) noexcept
{
#if defined(_WIN32)
    return _aligned_msize (p, align, 0);
#else
    (void) align;
    return blockSize (p);
#endif
}

void* allocate (std::size_t size)
{
    if (void* p = std::malloc (size == 0 ? 1 : size))
    {
        if (guarding)
        {
            ++count;
            held += blockSize (p);
        }
        return p;
    }
    throw std::bad_alloc {};
}
void release (void* p) noexcept
{
    if (p == nullptr)
        return;
    if (guarding)
    {
        const auto size = blockSize (p);
        held = size < held ? held - size : 0;
    }
    std::free (p);
}

void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    const auto align = static_cast<std::size_t> (alignment);
#if defined(_WIN32)
    void* p = _aligned_malloc (size == 0 ? 1 : size, align);
#else
    void* p = std::aligned_alloc (align, (size + align - 1) / align * align);
#endif
    if (p == nullptr)
        throw std::bad_alloc {};
    if (guarding)
    {
        ++count;
        held += alignedBlockSize (p, align);
    }
    return p;
}
void freeAligned (void* p, std::align_val_t alignment) noexcept
{
    if (p == nullptr)
        return;
    if (guarding)
    {
        const auto size = alignedBlockSize (p, static_cast<std::size_t> (alignment));
        held = size < held ? held - size : 0;
    }
#if defined(_WIN32)
    _aligned_free (p);
#else
    std::free (p);
#endif
}
} // namespace

namespace comp1::test
{
AllocationGuard::AllocationGuard()
{
    count = 0;
    held = 0;
    guarding = true;
}
AllocationGuard::~AllocationGuard() { guarding = false; }
std::size_t AllocationGuard::allocations() const { return count; }
std::size_t AllocationGuard::bytesHeld() const { return held; }
} // namespace comp1::test

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new (std::size_t size, std::align_val_t a) { return allocateAligned (size, a); }
void* operator new[] (std::size_t size, std::align_val_t a) { return allocateAligned (size, a); }

void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, std::align_val_t a) noexcept { freeAligned (p, a); }
void operator delete[] (void* p, std::align_val_t a) noexcept { freeAligned (p, a); }
void operator delete (void* p, std::size_t, std::align_val_t a) noexcept { freeAligned (p, a); }
void operator delete[] (void* p, std::size_t, std::align_val_t a) noexcept { freeAligned (p, a); }
