#pragma once

// How the null check compares two renders of a scene (docs/performance.md, "Null check"): bit-identical is "exact";
// otherwise the worst sample deviation, in dBFS, is held to the bar.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace comp1::test
{

struct Comparison
{
    bool exact = false;
    bool sameLength = true;
    double worstDb = -std::numeric_limits<double>::infinity(); // 20 log10 of the largest |a - b|

    // Within the bar: exact, or every sample within barDb of the other side's.
    bool passes (double barDb) const { return sameLength && (exact || worstDb <= barDb); }

    bool operator== (const Comparison&) const = default;
};

inline Comparison compare (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return { .exact = false, .sameLength = false, .worstDb = std::numeric_limits<double>::infinity() };
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (a[i]) - static_cast<double> (b[i])));
    if (worst == 0.0)
        return { .exact = true };
    return { .exact = false, .sameLength = true, .worstDb = 20.0 * std::log10 (worst) };
}

} // namespace comp1::test
