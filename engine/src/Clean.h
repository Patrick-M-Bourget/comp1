#pragma once

// The Clean Character: feedforward, after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
// Design — A Tutorial and Analysis", as dyn-lab models it (tools/dyn-lab/USAGE.md). Its gain computer and detector
// are kept apart from the rest of the Engine, so another Character can bring its own.

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

// The Gain Reduction in dB the Transfer Curve asks for at a Detection Level in dB: the paper's soft-knee curve
// (eq. 4) as the gain it takes off, unity below the Knee, 1/Ratio above it and a quadratic across it, capped at
// Range. Exactly 0 at 1:1 and below the Knee, -infinity dB (silence) included.
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
