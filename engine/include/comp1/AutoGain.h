#pragma once

// Auto Gain's estimate: worked out from the settings, never measured from the audio (ADR 0010), as dyn-lab models it
// (tools/dyn-lab/USAGE.md, "autogain"). It reads the Transfer Curve, the timing (Attack, Release, Auto Release and Hold)
// and the Character, and nothing that depends on the material or the routing. Public, so the editor reads the estimate
// the Engine plays from the same functions.

#include "comp1/Settings.h"
#include "comp1/TransferCurve.h"

#include <algorithm>
#include <cmath>

namespace comp1::clean
{

// The Transfer Curve's average Gain Reduction in dB over dyn-lab's reference level distribution, Detection Levels from
// -40 to 0 dB in 2 dB steps, each equally likely (REFERENCE_LEVELS; its USAGE.md gives the reasons), each read offsetDb
// higher: Auto Gain's estimate with the Timing Offset (autogain::estimateDb, below). Exactly 0 at 1:1, whatever the
// offset.
inline double averageGainReductionDb (const Curve& curve, double offsetDb)
{
    constexpr int lowestDb = -40, highestDb = 0, stepDb = 2;
    double sum = 0.0;
    int count = 0;
    for (int levelDb = lowestDb; levelDb <= highestDb; levelDb += stepDb, ++count)
        sum += gainReduction (levelDb + offsetDb, curve);
    return sum / count;
}

} // namespace comp1::clean

namespace comp1::autogain
{

// A Character's Timing Offset constants, dyn-lab's AUTO_GAIN_TIMING: the offset in dB at the reference timing (Attack
// 10 ms, Release 100 ms); its slopes per natural-log unit of Attack and of the effective Release, their product's,
// and each one's curvature; the factor Auto Release lengthens Release by on the reference signal; and the offset Hold
// moves it toward, and how fast, in ms of Hold.
struct TimingConstants
{
    double offsetDb, attack, release, cross, attackCurve, releaseCurve, autoReleaseFactor, heldDb, holdMs;
};

inline constexpr TimingConstants clean { -1.2690, -2.1654, 1.9583, 0.2911, -0.2014, -0.1824, 4.0, 3.5129, 6.6 };
inline constexpr TimingConstants vca { 0.9240, -0.5444, 0.5334, -0.0296, -0.0747, -0.0343, 4.55, 2.7740, 135.0 };
inline constexpr TimingConstants fet { 0.4186, -1.7589, 1.6848, 0.2602, -0.1775, -0.1390, 3.96, 4.8621, 11.6 };
inline constexpr TimingConstants opto { 0.4233, -1.4763, 0.9939, 0.0306, -0.0641, -0.0762, 4.55, 4.3769, 10.6 };

inline const TimingConstants& constantsOf (Character character)
{
    switch (character)
    {
        case Character::VCA: return vca;
        case Character::FET: return fet;
        case Character::Opto: return opto;
        case Character::Clean: break;
    }
    return clean;
}

// The Auto Gain grid's span, in ms, which the constants were fitted over: beyond it the Timing Offset holds its value
// at the edge (dyn-lab's AUTO_GAIN_GRID_ATTACKS and AUTO_GAIN_GRID_RELEASES).
inline constexpr double fastestAttackMs = 0.5, slowestAttackMs = 30.0;
inline constexpr double fastestReleaseMs = 20.0, slowestReleaseMs = 400.0;
inline constexpr double referenceAttackMs = 10.0, referenceReleaseMs = 100.0;

// The settings the estimate reads beyond the Transfer Curve: the target timing, in ms, and the Character.
struct Timing
{
    double attackMs = referenceAttackMs;
    double releaseMs = referenceReleaseMs;
    double holdMs = 0.0;
    bool autoRelease = false;
    Character character = Character::Clean;
};

// The Timing Offset in dB, dyn-lab's timing_offset: how much higher than each reference level the estimate reads the
// Transfer Curve. A quadratic in a = ln(Attack / 10 ms) and r = ln(effective Release / 100 ms), each held at the grid's
// edge, the effective Release being Release times the Character's factor with Auto Release on; then Hold moves it
// toward the Character's held offset, 1 - exp(-Hold / its hold time) of the way, when that is higher: Hold only ever
// holds the crests harder.
inline double timingOffsetDb (const Timing& timing)
{
    const auto& k = constantsOf (timing.character);
    const double releaseMs = timing.releaseMs * (timing.autoRelease ? k.autoReleaseFactor : 1.0);
    const double a = std::log (std::clamp (timing.attackMs, fastestAttackMs, slowestAttackMs)) - std::log (referenceAttackMs);
    const double r = std::log (std::clamp (releaseMs, fastestReleaseMs, slowestReleaseMs * k.autoReleaseFactor))
                     - std::log (referenceReleaseMs);
    double offsetDb = k.offsetDb + k.attack * a + k.release * r + k.cross * a * r + k.attackCurve * a * a + k.releaseCurve * r * r;
    if (timing.holdMs > 0.0 && k.heldDb > offsetDb)
        offsetDb += (k.heldDb - offsetDb) * (1.0 - std::exp (-timing.holdMs / k.holdMs));
    return offsetDb;
}

// Auto Gain's estimate in dB, dyn-lab's auto_gain: the Transfer Curve's average Gain Reduction over the reference levels,
// each read the Timing Offset higher. A fixed amount of arithmetic, with no allocation.
inline double estimateDb (const clean::Curve& curve, const Timing& timing)
{
    return clean::averageGainReductionDb (curve, timingOffsetDb (timing));
}

} // namespace comp1::autogain
