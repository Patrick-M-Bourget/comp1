#pragma once

// The Opto Character's own pieces, as dyn-lab models them (tools/dyn-lab/USAGE.md, "Opto" and "Colour"): FET's loop,
// stepped in two parts of the Gain Reduction with an attack that varies with the material and a release whose slow
// part slows with Opto's history, and its Colour mapping. Clean's detector keeps Hold and the branching between Attack
// and Release, and calls Opto's step for each move. Kept apart from the rest of the Engine, as Clean's, VCA's and FET's
// are.

#include "AutoRelease.h"
#include "Clean.h"
#include "Colour.h"
#include "Fet.h"

#include <algorithm>
#include <cmath>

namespace comp1::opto
{

// dyn-lab's OPTO_ATTACK_SLOWEST, OPTO_ATTACK_FASTEST and OPTO_ATTACK_SPAN_DB: Attack's time is scaled by the
// slowest at no overshoot, the fastest at the span and over, and a smoothstep between them in the scale's logarithm, so
// by 1 at half the span.
inline constexpr double attackSlowest = 2.0;
inline constexpr double attackFastest = 0.5;
inline constexpr double attackSpanDb = 20.0;
// dyn-lab's OPTO_FAST_SHARE: the fast part's share of the Gain Reduction, which releases at Release (or Auto Release's
// pole); the slow part holds the rest.
inline constexpr double fastShare = 0.6;
// dyn-lab's OPTO_SLOW_FRESH_MS and OPTO_SLOW_SEASONED_MS: the slow part's release time with no history and with a full
// one, and linearly between. Opto's own, not bounded by Release's range (ADR 0008).
inline constexpr double slowFreshMs = 500.0;
inline constexpr double slowSeasonedMs = 5000.0;
// dyn-lab's OPTO_HISTORY_CHARGE_MS and OPTO_HISTORY_DISCHARGE_MS: the history charges toward 1 while the Transfer
// Curve asks for Gain Reduction and discharges toward 0 while it doesn't, each covering 1 - 1/e of the way in its time.
inline constexpr double historyChargeMs = 3000.0;
inline constexpr double historyDischargeMs = 5000.0;

// The factor Attack's time is scaled by at an overshoot in dB, the Transfer Curve's Gain Reduction less the Gain
// Reduction held (dyn-lab's opto_attack_scale): attackSlowest at 0 dB and under, 1 at half attackSpanDb,
// attackFastest at attackSpanDb and over, with no corner at either end.
inline double attackScale (double overshootDb)
{
    const double x = std::clamp (overshootDb / attackSpanDb, 0.0, 1.0);
    return attackSlowest * std::pow (attackFastest / attackSlowest, x * x * (3.0 - 2.0 * x));
}

// Opto's two parts of one detection part's Gain Reduction and its history. It runs beside the detector on every
// Character: the history hears every sample and moves on every run, so a change to Opto starts from what the material
// has been, and the parts start from the Gain Reduction held, split by their shares, whenever Opto takes over from
// another Character. Never allocates.
class TwoStage
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        history.prepare (sampleRate, historyChargeMs, historyDischargeMs);
        reset();
    }

    void reset()
    {
        history.reset();
        fastDb = slowDb = 0.0;
        following = false;
        startSlowPole();
    }

    // On every sample: whether the Transfer Curve asked for Gain Reduction.
    void hear (bool asked) { history.hear (asked); }

    // At the start of each run: the history moves on by the run just heard, and the slow part's pole with it.
    void startRun()
    {
        history.startRun();
        startSlowPole();
    }

    // Another Character moved the Gain Reduction this sample: the parts no longer follow it.
    void leave() { following = false; }

    // One step of Opto from previousDb, the Gain Reduction the detector holds, with pole Attack's (when wantedDb, the
    // Transfer Curve's Gain Reduction at the detection sample's level, levelDb, is over it) or Release's, Auto Release's
    // included. Attacking, both parts move at Attack's time scaled by the overshoot; releasing, the fast part at pole and
    // the slow part at its own. Each moves one pole toward its share of what the gain computer asks for: fed back, FET's
    // loop solved over both parts at once (fet::loopFrom), capped as FET's is, each part never further than its pole to
    // the power fet::speedupMax moves toward its share of wantedDb; feedforward, wantedDb (dyn-lab's opto_step).
    // Returns the Gain Reduction, the parts' sum.
    double step (double previousDb, double levelDb, double wantedDb, double pole, const clean::Curve& curve, bool fedBack)
    {
        if (! following)
        {
            fastDb = fastShare * previousDb;
            slowDb = previousDb - fastDb;
            following = true;
        }
        const bool rising = wantedDb > previousDb;
        const double fastPole = rising ? std::pow (pole, 1.0 / attackScale (wantedDb - previousDb)) : pole;
        const double slowKeep = rising ? fastPole : slowPole;
        double asked = wantedDb;
        if (fedBack)
        {
            const double a = fastPole * fastDb + slowKeep * slowDb;
            const double b = fastShare * (1.0 - fastPole) + (1.0 - fastShare) * (1.0 - slowKeep);
            const double solved = fet::loopFrom (a, b, 1.0 - b, levelDb, curve);
            const double fastCap = std::pow (fastPole, fet::speedupMax), slowCap = std::pow (slowKeep, fet::speedupMax);
            const double cappedFast = fastCap * fastDb + (1.0 - fastCap) * fastShare * wantedDb;
            const double cappedSlow = slowCap * slowDb + (1.0 - slowCap) * (1.0 - fastShare) * wantedDb;
            const double capped = cappedFast + cappedSlow;
            if (rising ? capped < solved : capped > solved)
            {
                fastDb = cappedFast;
                slowDb = cappedSlow;
                return fastDb + slowDb;
            }
            asked = (solved - a) / b;
        }
        fastDb = fastPole * fastDb + (1.0 - fastPole) * fastShare * asked;
        slowDb = slowKeep * slowDb + (1.0 - slowKeep) * (1.0 - fastShare) * asked;
        return fastDb + slowDb;
    }

private:
    // The slow part's pole as the history stands: its time from slowFreshMs to slowSeasonedMs.
    void startSlowPole()
    {
        slowPole = std::exp (-1000.0 / ((slowFreshMs + (slowSeasonedMs - slowFreshMs) * history.value()) * sampleRate));
    }

    double sampleRate = 44100.0;
    RunCharge history;
    double fastDb = 0.0, slowDb = 0.0;
    bool following = false; // the parts sum to the Gain Reduction the detector holds
    double slowPole = 0.0;
};

// Opto's Colour mapping, the gentlest: dyn-lab's OPTO_COLOUR_WEIGHT, OPTO_COLOUR_WEIGHT_DB and OPTO_COLOUR_DRIVE_DB.
inline constexpr ColourMapping colour { .weight = 0.075, .weightDb = 10.0, .driveDb = 5.0 };

} // namespace comp1::opto
