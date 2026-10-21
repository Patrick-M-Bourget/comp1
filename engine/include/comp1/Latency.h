#pragma once

// The latency each Processing Mode reports, in host samples: plain functions of the mode and the sample rate, so the
// Plugin Shell and the tests ask from any thread. Zero Latency reports 0. High Quality reports one fixed latency per
// sample rate, whatever the other settings (ADR 0001), so the host's delay compensation never moves: the sum of three
// parts, each a delay at its own place in the chain. dyn-lab names the same parts with their reasons and prints the
// figures (python3 tools/dyn-lab/dynlab.py latency).

#include "comp1/Settings.h"

#include <algorithm>
#include <cmath>

namespace comp1
{

// The Oversampling reserve: the delay of Oversampling's two sections on the Wet path, before the Dry/Wet sum, a fixed
// count of host samples at every sample rate and factor, as ADR 0001's cap on the internal rate keeps it (dyn-lab's
// OVERSAMPLING_RESERVE_SAMPLES gives the reasoning).
inline constexpr int oversamplingReserveSamples = 128;

// Each Oversampling section's delay, half the reserve: its linear part is delayed by exactly this, and its residual's
// filters are padded to it (ADR 0009, dyn-lab's OVERSAMPLING_SECTION_SAMPLES).
inline constexpr int oversamplingSectionSamples = oversamplingReserveSamples / 2;

// The highest internal rate Oversampling runs at, in Hz (ADR 0001, dyn-lab's MAX_INTERNAL_RATE_HZ).
inline constexpr double maxInternalRateHz = 384000.0;

// How many times the host's sample rate an Oversampling runs at.
inline constexpr int factorOf (Oversampling oversampling) { return 1 << static_cast<int> (oversampling); }

// The largest Oversampling allowed at sampleRate: the largest whose internal rate is at or under maxInternalRateHz.
// For the editor to offer only the allowed choices (#27).
inline Oversampling maxOversampling (double sampleRate)
{
    auto allowed = Oversampling::x1;
    for (const auto oversampling : { Oversampling::x2, Oversampling::x4, Oversampling::x8 })
        if (factorOf (oversampling) * sampleRate <= maxInternalRateHz)
            allowed = oversampling;
    return allowed;
}

// The Oversampling a setting plays at sampleRate: the setting, or the cap when it is over it.
inline Oversampling oversamplingInForce (Oversampling setting, double sampleRate)
{
    return std::min (setting, maxOversampling (sampleRate));
}

// The Lookahead budget: Lookahead's top, so every Lookahead fits within it.
inline constexpr double lookaheadBudgetMs = parameterRanges::lookahead.high;

// Reserved for the Limiter (#18): its own lookahead, about 1 ms (ADR 0001). Until then a plain delay of this length,
// last in the chain, holds its place.
inline constexpr double limiterReserveMs = 1.0;

// A time in host samples, rounded up so the time always fits.
inline int wholeSamples (double ms, double sampleRate)
{
    return static_cast<int> (std::ceil (ms * sampleRate / 1000.0));
}

inline int lookaheadBudgetSamples (double sampleRate) { return wholeSamples (lookaheadBudgetMs, sampleRate); }
inline int limiterReserveSamples (double sampleRate) { return wholeSamples (limiterReserveMs, sampleRate); }

// The Wet path's delay up to the Dry/Wet sum: the Oversampling reserve and the Lookahead budget in High Quality, 0 in
// Zero Latency. What the Dry path is delayed by (#11).
inline int wetPathLatencySamples (ProcessingMode mode, double sampleRate)
{
    return mode == ProcessingMode::HighQuality ? oversamplingReserveSamples + lookaheadBudgetSamples (sampleRate) : 0;
}

// The latency the Engine's output has in mode, at sampleRate: the Wet path's, then the Limiter reserve. What the
// Plugin Shell reports to the host, and what Global Bypass is delayed by (#12).
inline int latencySamples (ProcessingMode mode, double sampleRate)
{
    return mode == ProcessingMode::HighQuality ? wetPathLatencySamples (mode, sampleRate) + limiterReserveSamples (sampleRate) : 0;
}

} // namespace comp1
