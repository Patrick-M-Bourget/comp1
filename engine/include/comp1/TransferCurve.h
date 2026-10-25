#pragma once

// The Transfer Curve: the Gain Reduction every Character's gain computer asks for at a Detection Level, from Threshold,
// Ratio, Knee and Range. Public, so the editor draws the curve the Engine plays from the same function.

#include "comp1/Settings.h"

namespace comp1::clean
{

// The settings the gain computer reads.
struct Curve
{
    double thresholdDb = 0.0;
    double inverseRatio = 1.0; // 1/Ratio: 0 at ∞:1
    double kneeDb = 0.0;
    double rangeDb = 0.0;
};

// The gain computer's settings, from Settings: the stored Threshold, Ratio, Knee and Range.
inline Curve curveOf (const Settings& settings)
{
    return { .thresholdDb = settings.thresholdDb,
             .inverseRatio = 1.0 / settings.ratio,
             .kneeDb = settings.kneeDb,
             .rangeDb = settings.rangeDb };
}

// The gain computer's settings the compressor plays, settled: curveOf's, with Threshold and 1/Ratio from the Comp
// Curve at Comp while Comp Follow is on (ADR 0004).
inline Curve playedCurveOf (const Settings& settings)
{
    auto curve = curveOf (settings);
    if (settings.compFollow)
    {
        curve.thresholdDb = compThresholdDb (settings.compCurve, settings.comp);
        curve.inverseRatio = compInverseRatio (settings.compCurve, settings.comp);
    }
    return curve;
}

// The Gain Reduction in dB the Transfer Curve asks for at a Detection Level in dB: Giannoulis, Massberg & Reiss's
// soft-knee curve (eq. 4; Clean.h names the paper) as the gain it takes off, unity below the Knee, 1/Ratio above it
// and a quadratic across it, capped at Range. Exactly 0 at 1:1 and below the Knee, -infinity dB (silence) included.
inline double gainReduction (double levelDb, const Curve& curve)
{
    const double over = levelDb - curve.thresholdDb;
    const double halfKnee = curve.kneeDb / 2.0;
    const double slope = 1.0 - curve.inverseRatio;
    double reduction = 0.0;
    if (over <= -halfKnee)
        return 0.0;
    if (over < halfKnee)
        reduction = slope * (over + halfKnee) * (over + halfKnee) / (2.0 * curve.kneeDb);
    else
        reduction = slope * over;
    return reduction < curve.rangeDb ? reduction : curve.rangeDb;
}

} // namespace comp1::clean
