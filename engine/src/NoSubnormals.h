#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #define COMP1_SUBNORMALS_X86 1
#elif defined(__aarch64__)
    #define COMP1_SUBNORMALS_ARM64 1
#endif

namespace comp1
{

// While alive, the current thread's floating-point unit flushes subnormal numbers to zero, in and out: a
// detector or filter decaying into silence would otherwise reach them, and on x64 every operation on one costs
// many times a normal one. The thread's mode is restored on the way out, so the caller's own arithmetic is left
// as it was. On x64 and arm64, which comp1 builds for; elsewhere it does nothing.
class NoSubnormals
{
public:
    NoSubnormals() : saved (read())
    {
#if COMP1_SUBNORMALS_X86
        write (saved | flushToZero | denormalsAreZero);
#elif COMP1_SUBNORMALS_ARM64
        write (saved | flushToZero);
#endif
    }

    ~NoSubnormals() { write (saved); }

    NoSubnormals (const NoSubnormals&) = delete;
    NoSubnormals& operator= (const NoSubnormals&) = delete;

private:
#if COMP1_SUBNORMALS_X86
    static constexpr std::uint64_t flushToZero = 0x8000, denormalsAreZero = 0x0040; // MXCSR FTZ and DAZ
    static std::uint64_t read() { return _mm_getcsr(); }
    static void write (std::uint64_t mode) { _mm_setcsr (static_cast<unsigned int> (mode)); }
#elif COMP1_SUBNORMALS_ARM64
    static constexpr std::uint64_t flushToZero = std::uint64_t { 1 } << 24; // FPCR FZ, which flushes inputs too
    static std::uint64_t read()
    {
        std::uint64_t mode;
        asm volatile ("mrs %0, fpcr" : "=r"(mode));
        return mode;
    }
    static void write (std::uint64_t mode) { asm volatile ("msr fpcr, %0" : : "r"(mode)); }
#else
    static std::uint64_t read() { return 0; }
    static void write (std::uint64_t) {}
#endif

    std::uint64_t saved;
};

} // namespace comp1
