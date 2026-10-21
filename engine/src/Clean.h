#pragma once

// The Clean Character: feedforward, after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
// Design — A Tutorial and Analysis", as dyn-lab models it (tools/dyn-lab/USAGE.md). Its gain computer and detector
// are kept apart from the rest of the Engine, so another Character can bring its own.

#include <cmath>

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

// The settings the detector reads, at the sample rate.
struct Timing
{
    double attackPole = 0.0;  // each a one-pole coefficient that covers 1 - 1/e of a step in its time
    double releasePole = 0.0;
    int holdSamples = 0;
};

// A one-pole coefficient that covers 1 - 1/e of a step in ms milliseconds at sampleRate.
inline double pole (double ms, double sampleRate) { return std::exp (-1000.0 / (ms * sampleRate)); }

inline Timing timing (double attackMs, double releaseMs, double holdMs, double sampleRate)
{
    return { .attackPole = pole (attackMs, sampleRate),
             .releasePole = pole (releaseMs, sampleRate),
             .holdSamples = static_cast<int> (std::lround (holdMs * sampleRate / 1000.0)) };
}

// The paper's smooth branching peak detector (eq. 12), in the log domain on the gain computer's output: it smooths the
// Gain Reduction asked for, rising with Attack and falling with Release, each one pole. Hold keeps the Gain Reduction
// where it is for Hold whenever it stops rising, before Release begins; a rise at any point, mid-Attack included,
// restarts it. Not the decoupled detector (eq. 17), whose Release runs through the attack pole too, so gain comes back
// more slowly than Release says.
class Detector
{
public:
    void reset()
    {
        gainReductionDb = 0.0;
        held = 0;
    }

    // Takes the Gain Reduction asked for, in dB, and returns the one to apply.
    double process (double wantedDb, const Timing& t)
    {
        double k = t.releasePole;
        if (wantedDb > gainReductionDb)
        {
            held = 0;
            k = t.attackPole;
        }
        else if (held < t.holdSamples)
        {
            ++held;
            return gainReductionDb;
        }
        gainReductionDb = k * gainReductionDb + (1.0 - k) * wantedDb;
        return gainReductionDb;
    }

private:
    double gainReductionDb = 0.0;
    int held = 0;
};

} // namespace comp1::clean
