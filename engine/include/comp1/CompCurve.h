#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace comp1
{

// A point a Comp Curve passes through: Comp in %, and the Threshold in dB and Ratio (:1) it sets there.
struct CompPoint
{
    double comp = 0.0;
    double thresholdDb = 0.0;
    double ratio = 1.0;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal" // exactly equal: settings that changed nothing are skipped
#endif
    constexpr bool operator== (const CompPoint&) const = default;
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
};

// A Style's Comp Curve, dyn-lab's form (tools/dyn-lab/USAGE.md, "Comp"): three points, at Comp 0%, the Style's default
// Comp and 100%, with Threshold and 1/Ratio each running in a straight line from one point to the next. The first
// point's Ratio is 1:1, so 0% compresses nothing; each point's Threshold is below the one before and its Ratio above,
// so Threshold falls and Ratio rises with Comp, and each Threshold on the Comp Curve has one Comp. The middle point is the
// Style's default Threshold and Ratio, which its default Comp lands on exactly.
struct CompCurve
{
    std::array<CompPoint, 3> points {};

    constexpr bool operator== (const CompCurve&) const = default;
};

namespace detail
{
// The two points comp (clamped to the Comp Curve's span) falls between, and how far from the first to the second, 0 to 1.
struct Between
{
    const CompPoint& from;
    const CompPoint& to;
    double share;
};

inline Between between (const CompCurve& compCurve, double comp)
{
    const auto& p = compCurve.points;
    comp = std::clamp (comp, p.front().comp, p.back().comp);
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        if (comp <= p[i + 1].comp)
            return { p[i], p[i + 1], (comp - p[i].comp) / (p[i + 1].comp - p[i].comp) };
    return { p[p.size() - 2], p.back(), 1.0 };
}

// a and b mixed as (1 - u) a + u b: exactly a at u = 0 and exactly b at u = 1, so the Comp Curve meets its points.
inline double mix (double a, double b, double u) { return (1.0 - u) * a + u * b; }
} // namespace detail

// The Threshold in dB a Comp Curve sets at comp (%).
inline double compThresholdDb (const CompCurve& compCurve, double comp)
{
    const auto at = detail::between (compCurve, comp);
    return detail::mix (at.from.thresholdDb, at.to.thresholdDb, at.share);
}

// The 1/Ratio a Comp Curve sets at comp (%), as the Engine glides Ratio: 0 for ∞:1.
inline double compInverseRatio (const CompCurve& compCurve, double comp)
{
    const auto at = detail::between (compCurve, comp);
    return detail::mix (1.0 / at.from.ratio, 1.0 / at.to.ratio, at.share);
}

// The Ratio (:1) a Comp Curve sets at comp (%), infinity for ∞:1.
inline double compRatio (const CompCurve& compCurve, double comp)
{
    const double inverse = compInverseRatio (compCurve, comp);
    return inverse <= 0.0 ? std::numeric_limits<double>::infinity() : 1.0 / inverse;
}

// The Comp (%) on a Comp Curve nearest a Threshold in dB: the one that sets it, clamped to the Comp Curve's span, so a Threshold
// above the Comp Curve's start is its first Comp and one below its end its last.
inline double compNearestThreshold (const CompCurve& compCurve, double thresholdDb)
{
    const auto& p = compCurve.points;
    if (thresholdDb >= p.front().thresholdDb)
        return p.front().comp;
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        if (thresholdDb >= p[i + 1].thresholdDb)
            return detail::mix (p[i].comp, p[i + 1].comp, (p[i].thresholdDb - thresholdDb) / (p[i].thresholdDb - p[i + 1].thresholdDb));
    return p.back().comp;
}

} // namespace comp1
