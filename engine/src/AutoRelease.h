#pragma once

// Auto Release, as dyn-lab models it (tools/dyn-lab/USAGE.md, "Auto Release"): Release lengthens after sustained
// compression and stays short after transients. It sits beside a Character's detector, which asks it for the release
// pole to use, so every Character can have it (ADR 0002).

#include <cmath>

namespace comp1
{

// A state from 0 to 1 that follows how much of the time the Transfer Curve asks for Gain Reduction, stepped on the
// run grid: each sample counts whether it asked, and at the start of each run the state charges toward 1 for the
// samples of the run just heard that asked and discharges toward 0 for the rest, each covering 1 - 1/e of the way in
// its time. Auto Release's sustain state is one, and Opto's history another (Opto.h).
class RunCharge
{
public:
    void prepare (double sampleRate, double chargeMs, double dischargeMs)
    {
        charge = std::exp (-1000.0 / (chargeMs * sampleRate));
        discharge = std::exp (-1000.0 / (dischargeMs * sampleRate));
        reset();
    }

    void reset()
    {
        state = 0.0;
        heard = compressing = 0;
    }

    // On every sample: whether the Transfer Curve asked for Gain Reduction.
    void hear (bool asked)
    {
        ++heard;
        compressing += asked ? 1 : 0;
    }

    // At the start of each run: the state moves on by the samples heard since the last.
    void startRun()
    {
        state = 1.0 - (1.0 - state) * std::pow (charge, compressing);
        state *= std::pow (discharge, heard - compressing);
        heard = compressing = 0;
    }

    double value() const { return state; }

private:
    double charge = 0.0, discharge = 0.0; // the state's poles, a sample each
    double state = 0.0;                   // 0 to 1
    int heard = 0;                        // this run's samples
    int compressing = 0;                  // and those of them that asked for Gain Reduction
};

class AutoRelease
{
public:
    // dyn-lab's AUTO_RELEASE_MULTIPLIER: after sustained compression, Release is this many times as long.
    static constexpr double multiplier = 5.0;
    // dyn-lab's AUTO_RELEASE_CHARGE_MS and AUTO_RELEASE_DISCHARGE_MS: the sustain state charges toward 1 while the
    // Transfer Curve asks for Gain Reduction, and discharges toward 0 while it doesn't, each covering 1 - 1/e of the
    // way in its time.
    static constexpr double chargeMs = 1000.0;
    static constexpr double dischargeMs = 2000.0;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        sustain.prepare (sampleRate, chargeMs, dischargeMs);
        reset();
    }

    void reset()
    {
        sustain.reset();
        on = false;
    }

    // On every sample: whether the Transfer Curve asked for Gain Reduction.
    void hear (bool asked) { sustain.hear (asked); }

    // At the start of each run of the grid the detector hears on: the sustain state moves on by the samples heard
    // since the last, charging for those that asked for Gain Reduction and discharging for the rest, and, with Auto
    // Release on, the release pole is worked out again from it. The sustain state moves on whether Auto Release is on
    // or off, so it follows the material from the moment it is turned on. A run is plugkit::runSamples long, except
    // where the detector's delay moves (Lookahead, or a Processing Mode switch), which lengthens or shortens one.
    void startRun (double releaseMs, bool autoReleaseOn)
    {
        sustain.startRun();
        on = autoReleaseOn;
        if (on)
            pole = std::exp (-1000.0 / (releaseMs * (1.0 + (multiplier - 1.0) * sustain.value()) * sampleRate));
    }

    // The release pole to use: Auto Release's while it is on, otherwise releasePole, Release's own.
    double release (double releasePole) const { return on ? pole : releasePole; }

private:
    double sampleRate = 44100.0;
    RunCharge sustain;
    bool on = false;
    double pole = 0.0;
};

} // namespace comp1
