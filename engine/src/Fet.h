#pragma once

// The FET Character's own pieces, as dyn-lab models them (tools/dyn-lab/USAGE.md, "FET" and "Colour"): its feedback
// loop, solved within each sample, which steps Clean's detector, and its Colour mapping. Kept apart from the rest of
// the Engine, as Clean's and VCA's are.

#include "Clean.h"
#include "Colour.h"

#include <algorithm>
#include <cmath>

namespace comp1::fet
{

// dyn-lab's FET_SPEEDUP_MAX: the loop never moves the Gain Reduction faster than one pole of the time over this.
inline constexpr double speedupMax = 4.0;

// The loop, solved within the sample: the Gain Reduction G that one step of a one-pole detector reaches from
// previousDb when its gain computer reads the detection sample's level, levelDb, less G itself:
// G = pole · previous + (1 - pole) · w(level - G). w is the Transfer Curve's inverse in the output's domain, capped at
// Range, so at steady state the output is the Transfer Curve's. w never falls as its level rises, so G is unique and
// lies between previousDb and the steady Gain Reduction; it is found region by region of the Transfer Curve in closed
// form (dyn-lab's fet_loop).
inline double loop (double previousDb, double levelDb, double pole, const clean::Curve& curve)
{
    const double a = pole * previousDb, b = 1.0 - pole;
    const double t = curve.thresholdDb, knee = curve.kneeDb, q = curve.inverseRatio, half = knee / 2.0;
    double solved = 0.0;
    if (levelDb - a <= t - half)
        solved = a; // below the Knee in the output's domain, where w is 0
    else if (knee > 0.0 && a + b * (1.0 - q) * half >= levelDb - t - q * half)
    {
        // Across the Knee. With u = x - T + W/2 for the input x whose output is the loop's, the output's y - T + W/2 is
        // u + (q - 1)u²/2W, so the loop is pole·(q - 1)u²/2W + u = level - T + W/2 - a, a quadratic in u.
        const double e = levelDb - t + half - a;
        const double c = pole * (q - 1.0) / (2.0 * knee);
        const double u = 2.0 * e / (1.0 + std::sqrt (std::max (0.0, 1.0 + 4.0 * c * e)));
        solved = a + b * (1.0 - q) * u * u / (2.0 * knee);
    }
    else
        // Above it, w is (Ratio - 1)(y - T), written with 1/Ratio so that ∞:1 holds the output at Threshold.
        solved = (q * a + b * (1.0 - q) * (levelDb - t)) / (q + b * (1.0 - q));
    // Range caps w, and so the loop's solution.
    return std::min (solved, a + b * curve.rangeDb);
}

// One step of FET's detector from previousDb, pole Attack's or Release's: the loop's solution, but never further than
// one pole of the time over speedupMax moves toward wantedDb, the Transfer Curve's Gain Reduction at the detection
// sample's level, which is the loop's steady state (dyn-lab's fet_step).
inline double step (double previousDb, double levelDb, double wantedDb, double pole, const clean::Curve& curve)
{
    const double solved = loop (previousDb, levelDb, pole, curve);
    const double fastest = std::pow (pole, speedupMax);
    const double capped = fastest * previousDb + (1.0 - fastest) * wantedDb;
    return wantedDb > previousDb ? std::min (solved, capped) : std::max (solved, capped);
}

// FET's Colour mapping, steeper than VCA's: dyn-lab's FET_COLOUR_WEIGHT, FET_COLOUR_WEIGHT_DB and FET_COLOUR_DRIVE_DB.
inline constexpr ColourMapping colour { .weight = 0.25, .weightDb = 10.0, .driveDb = 4.0 };

} // namespace comp1::fet
