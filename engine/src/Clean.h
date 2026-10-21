#pragma once

// The Clean Character: feedforward, after Giannoulis, Massberg & Reiss (2012), "Digital Dynamic Range Compressor
// Design — A Tutorial and Analysis", as dyn-lab models it (tools/dyn-lab/USAGE.md). Its gain computer and detector
// are kept apart from the rest of the Engine, so another Character can bring its own.

#include "AutoRelease.h"

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

// The Auto Gain estimate in dB: the negated average Gain Reduction of the Transfer Curve over dyn-lab's reference
// level distribution, Detection Levels from -40 to 0 dB in 2 dB steps, each equally likely (REFERENCE_LEVELS; its
// USAGE.md gives the reasons). Worked out from the settings alone, never measured from the audio.
inline double autoGainDb (const Curve& curve)
{
    constexpr int lowestDb = -40, highestDb = 0, stepDb = 2;
    double sum = 0.0;
    int count = 0;
    for (int levelDb = lowestDb; levelDb <= highestDb; levelDb += stepDb, ++count)
        sum += gainReduction (levelDb, curve);
    return sum / count;
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
// more slowly than Release says. Release takes its pole from Auto Release, which hears every sample; Hold runs first
// either way.
class Detector
{
public:
    void prepare (double sampleRate) { autoRelease.prepare (sampleRate); }

    void reset()
    {
        gainReductionDb = 0.0;
        held = 0;
        autoRelease.reset();
    }

    // At the start of each run: Auto Release moves on by the run just heard (AutoRelease::startRun).
    void startRun (double releaseMs, bool autoReleaseOn) { autoRelease.startRun (releaseMs, autoReleaseOn); }

    // Takes the Gain Reduction asked for, in dB, and returns the one to apply.
    double process (double wantedDb, const Timing& t)
    {
        return process (wantedDb, t, [wantedDb] (double previousDb, double k) { return k * previousDb + (1.0 - k) * wantedDb; });
    }

    // The same, with each step that moves the Gain Reduction worked out by step (previous Gain Reduction in dB, pole)
    // in place of the one pole: a Character whose detector is fed back (FET) steps by its loop, branching here as
    // Clean does.
    template <typename Step>
    double process (double wantedDb, const Timing& t, Step&& step)
    {
        autoRelease.hear (wantedDb > 0.0);
        double k = autoRelease.release (t.releasePole);
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
        gainReductionDb = step (gainReductionDb, k);
        return gainReductionDb;
    }

private:
    double gainReductionDb = 0.0;
    int held = 0;
    AutoRelease autoRelease;
};

} // namespace comp1::clean
