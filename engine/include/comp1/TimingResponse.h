#pragma once

// The Timing Response: the compressor's Gain Reduction over time for a test burst, its rise as the burst starts and its
// recovery once it stops, which the editor's Timing Response inset draws. Public, so the editor draws it from the
// Engine's own detector.

#include "comp1/Settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace comp1
{

struct TimingResponse
{
    // The times it holds, on a log axis: pointsPerDecade a decade from earliestMs, Attack's shortest, to about 10 s,
    // past Release's longest with room for Auto Release and Opto's slow part.
    static constexpr double earliestMs = 0.005;
    static constexpr int pointsPerDecade = 60;
    static constexpr int points = 379;
    // The burst: burstOverDb over the played Threshold, at most 0 dBFS, for burstSeconds.
    static constexpr double burstOverDb = 20.0;
    static constexpr double burstSeconds = 1.0;

    // The time in ms of point (0 to points - 1).
    static double timeMsAt (int point) { return earliestMs * std::pow (10.0, static_cast<double> (point) / pointsPerDecade); }
    // The sample a point reads at sampleRate, counted from the burst's start or end: the one under its time.
    static long sampleAt (int point, double sampleRate) { return static_cast<long> (std::floor (timeMsAt (point) * sampleRate / 1000.0)); }

    // The burst's sample peak, in dBFS.
    double burstLevelDb = 0.0;
    // The Gain Reduction in dB at each point's time after the burst starts; from burstSeconds on, the Gain Reduction at
    // the burst's end.
    std::array<double, points> rise {};
    // And after the burst ends. With Auto Release on, its band's fastest edge, the recovery at Release: the recovery
    // Auto Release plays depends on what came before the burst, so the band is what is drawn, and it isn't worked out.
    std::array<double, points> recovery {};

    // A range a curve can take, as Gain Reduction in dB at each point's time: its fastest edge and its slowest.
    struct Band
    {
        std::array<double, points> fastest {}, slowest {};
    };
    // On a Character with Opto's timing, the range the rise can take as the overshoot scales Attack: the rise with the
    // scale held at each of its ends, Attack x 0.5 the fastest and Attack x 2 the slowest, each one pole at that time
    // with the Character's loop solved as on the rise. None on the other Characters.
    std::optional<Band> riseBand;
    // With Auto Release on, the range the recovery can take as Auto Release scales Release: the recovery at Release, as
    // after no sustained compression, the fastest, and at 5 x Release, as after a long stretch of it, the slowest; each
    // from the detector as the burst leaves it, at its Release from the burst's end. The recovery the Engine plays lies
    // between them. None with Auto Release off.
    std::optional<Band> recoveryBand;
    // On a Character with Opto's timing, the times in ms after the burst's end that the slow part of its release takes,
    // Hold plus its time with no history to Hold plus its time with a full one: Opto's own, which Release doesn't move.
    // None on the other Characters.
    std::optional<std::pair<double, double>> slowPartMs;

    // The rise and the recovery at any time in ms, read between points along log time, holding at the ends.
    double riseAt (double ms) const { return at (rise, ms); }
    double recoveryAt (double ms) const { return at (recovery, ms); }

private:
    static double at (const std::array<double, points>& values, double ms)
    {
        const double position = ms > 0.0 ? std::log10 (ms / earliestMs) * pointsPerDecade : 0.0;
        if (position <= 0.0)
            return values.front();
        if (position >= points - 1)
            return values.back();
        const auto i = static_cast<std::size_t> (position);
        const double between = position - static_cast<double> (i);
        return values[i] + between * (values[i + 1] - values[i]);
    }
};

// The Timing Response at settings and sampleRate: a burst whose every sample has the same magnitude (a square wave), so
// it reads its level on every Character but VCA, whose RMS reads it 3 dB over, then silence. Only the compressor's
// level-following stage runs, the detector of the Character settings name (Clean, VCA, FET or Opto), from rest, with
// its Attack, Release, Auto Release and Hold, and the Transfer Curve it plays (Comp Follow's Threshold and Ratio while
// it is on, the stored Knee and Range), hearing the burst as an Internal Detection Source does; nothing else of the
// Engine runs, and no setting glides. So it is the Gain Reduction the Engine applies to the same burst at Detection
// Source Internal with the Detection EQ off. The bands run the same detector again: Opto's rise twice more, and with
// Auto Release on the recovery's two edges in place of the recovery, from the detector as the burst leaves it. Never
// allocates; its cost grows with sampleRate, at most about 29 ms at 48 kHz on an Apple M3 (Opto, Auto Release, Release
// 5000 ms) and four times that at 192 kHz, so the editor works it out away from the message thread, and always at
// drawnSampleRate.
TimingResponse timingResponse (const Settings& settings, double sampleRate);

// The sample rate the editor works the Timing Response out at, whatever the host plays at: the shape of the response
// barely changes with the rate, and its cost grows with it.
inline constexpr double drawnSampleRate = 48000.0;

} // namespace comp1
