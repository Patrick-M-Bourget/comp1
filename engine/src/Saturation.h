#pragma once

// The saturation module: one soft-clip shape, its nonlinear residual and the residual's antiderivative, and
// first-order antiderivative anti-aliasing (ADAA) of the residual only (ADR 0005), as dyn-lab models them
// (tools/dyn-lab/USAGE.md, "Saturation"). It is kept apart from the rest of the Engine, as a Character's gain computer
// is: the Saturation stage drives it with Drive, and Colour (#19) with a drive of its own, sample by sample. It
// assumes nothing about the sample rate, so it runs as well oversampled (#10).

#include <cmath>
#include <numbers>

namespace comp1::saturation
{

// The soft-clip shape, f(u): exactly u for |u| <= 1; above it, 1 + tanh(|u| - 1) with u's sign, which leaves the line
// with its slope (1) and curvature (0), so it is C²-smooth, and saturates toward 2.
inline double shape (double u)
{
    const double t = std::abs (u) - 1.0;
    if (t <= 0.0)
        return u;
    return std::copysign (1.0 + std::tanh (t), u);
}

// The shape's nonlinear residual, r(u) = f(u) - u: exactly 0 for |u| <= 1, then tanh(t) - t with u's sign, where
// t = |u| - 1.
inline double residual (double u)
{
    const double t = std::abs (u) - 1.0;
    if (t <= 0.0)
        return 0.0;
    return -std::copysign (t - std::tanh (t), u);
}

// R(u), the residual's antiderivative: 0 for |u| <= 1, then ln cosh t - t²/2 (even, as r is odd), with t = |u| - 1.
// ln cosh t is worked out as t + ln(1 + e^-2t) - ln 2, which never overflows.
inline double residualAntiderivative (double u)
{
    const double t = std::abs (u) - 1.0;
    if (t <= 0.0)
        return 0.0;
    return t + std::log1p (std::exp (-2.0 * t)) - std::numbers::ln2 - t * t / 2.0;
}

// One channel's ADAA of the residual: each driven sample's residual is the average of r along the line from the last
// driven sample to this one, (R(u[n]) - R(u[n-1])) / (u[n] - u[n-1]). The caller adds it to the linear part, which
// passes sample-aligned. Never allocates.
class AntiAliased
{
public:
    // Two driven samples closer than this count as equal, and the residual is r at their midpoint instead: far above
    // where the quotient's rounding matters, and the midpoint's error, second order in the gap, is under 1e-13 of
    // the residual's curvature (dyn-lab's ADAA_EQUAL_WITHIN).
    static constexpr double equalWithin = 1.0e-6;

    // The anti-aliased residual for the driven sample u, given the last one. Finite whenever u is.
    double process (double u)
    {
        if (stale)
            previousAntiderivative = residualAntiderivative (previous);
        const double antiderivative = residualAntiderivative (u);
        const double gap = u - previous;
        const double out = std::abs (gap) < equalWithin ? residual ((u + previous) / 2.0) : (antiderivative - previousAntiderivative) / gap;
        previous = u;
        previousAntiderivative = antiderivative;
        stale = false;
        return out;
    }

    // Takes u as the last driven sample without working out a residual: the state process() would leave, for a
    // stretch whose residual isn't heard.
    void follow (double u)
    {
        previous = u;
        stale = true;
    }

private:
    double previous = 0.0;
    double previousAntiderivative = 0.0;
    bool stale = false; // previousAntiderivative is still to be worked out from previous
};

} // namespace comp1::saturation
