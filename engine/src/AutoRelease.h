#pragma once

// Auto Release, as dyn-lab models it (tools/dyn-lab/USAGE.md, "Auto Release"): Release lengthens after sustained
// compression and stays short after transients. It sits beside a Character's detector, which asks it for the release
// pole to use, so every Character can have it (ADR 0002).

#include <cmath>

namespace comp1
{

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
        charge = std::exp (-1000.0 / (chargeMs * sampleRate));
        discharge = std::exp (-1000.0 / (dischargeMs * sampleRate));
        reset();
    }

    void reset()
    {
        sustain = 0.0;
        heard = compressing = 0;
        on = false;
    }

    // On every sample: whether the Transfer Curve asked for Gain Reduction.
    void hear (bool asked)
    {
        ++heard;
        compressing += asked ? 1 : 0;
    }

    // At the start of each run of the grid the detector hears on: the sustain state moves on by the samples heard
    // since the last, charging for those that asked for Gain Reduction and discharging for the rest, and, with Auto
    // Release on, the release pole is worked out again from it. The sustain state moves on whether Auto Release is on
    // or off, so it follows the material from the moment it is turned on. A run is plugkit::runSamples long, except
    // where the detector's delay moves (Lookahead, or a Processing Mode switch), which lengthens or shortens one.
    void startRun (double releaseMs, bool autoReleaseOn)
    {
        sustain = 1.0 - (1.0 - sustain) * std::pow (charge, compressing);
        sustain *= std::pow (discharge, heard - compressing);
        heard = compressing = 0;
        on = autoReleaseOn;
        if (on)
            pole = std::exp (-1000.0 / (releaseMs * (1.0 + (multiplier - 1.0) * sustain) * sampleRate));
    }

    // The release pole to use: Auto Release's while it is on, otherwise releasePole, Release's own.
    double release (double releasePole) const { return on ? pole : releasePole; }

private:
    double sampleRate = 44100.0;
    double charge = 0.0, discharge = 0.0; // the sustain state's poles, a sample each
    double sustain = 0.0;                 // 0 to 1
    int heard = 0;                        // this run's samples
    int compressing = 0;                  // and those of them that asked for Gain Reduction
    bool on = false;
    double pole = 0.0;
};

} // namespace comp1
