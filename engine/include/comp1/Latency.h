#pragma once

// The latency each Processing Mode reports, in host samples: plain functions of the mode and the sample rate, so the
// Plugin Shell and the tests ask from any thread. Zero Latency reports 0. High Quality reports one fixed latency per
// sample rate, whatever the other settings (ADR 0001), so the host's delay compensation never moves: the sum of three
// parts, each a delay at its own place in the chain. dyn-lab names the same parts with their reasons and prints the
// figures (python3 tools/dyn-lab/dynlab.py latency).

#include "comp1/Settings.h"

#include <cmath>

namespace comp1
{

// Reserved for Oversampling (#10): its filters' delay, a fixed count of host samples at every sample rate, as ADR
// 0001's cap on the internal rate keeps it (dyn-lab's OVERSAMPLING_RESERVE_SAMPLES gives the reasoning). Until then a
// plain delay of this length on the Wet path, before the Dry/Wet sum, holds its place.
inline constexpr int oversamplingReserveSamples = 128;

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
// Zero Latency. What the Dry path is delayed by.
inline int wetPathLatencySamples (ProcessingMode mode, double sampleRate)
{
    return mode == ProcessingMode::HighQuality ? oversamplingReserveSamples + lookaheadBudgetSamples (sampleRate) : 0;
}

// The latency the Engine's output has in mode, at sampleRate: the Wet path's, then the Limiter reserve. What the
// Plugin Shell reports to the host, and what Global Bypass is delayed by.
inline int latencySamples (ProcessingMode mode, double sampleRate)
{
    return mode == ProcessingMode::HighQuality ? wetPathLatencySamples (mode, sampleRate) + limiterReserveSamples (sampleRate) : 0;
}

} // namespace comp1
