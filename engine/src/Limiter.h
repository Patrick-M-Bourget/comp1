#pragma once

// The Limiter: the last stage, after Output Gain, which never lets the output pass the Ceiling. On every channel, with
// one gain from the loudest channel. Each Processing Mode has its own, as dyn-lab models them (tools/dyn-lab/USAGE.md,
// "Limiter"), with its figures (python3 tools/dyn-lab/dynlab.py limiter):
//
// - Zero Latency: a zero-attack gain computer on the sample peak. Each sample's Gain Reduction brings it to the
//   Ceiling at once and releases with one pole in dB; then a safety clip at the Ceiling. No delay, and no sample's
//   magnitude ever passes the Ceiling.
// - High Quality: an 8x true-peak estimator, aiming the headroom under the Ceiling, then over a lookahead window W, a
//   sliding maximum of the Gain Reduction asked for over W + 1 samples, the release, and their average over W + 1
//   samples, so the gain moves smoothly and is fully down when a peak reaches the audio. The estimator's delay and W
//   fill the Limiter reserve (comp1/Latency.h) exactly: the audio is delayed by it, on or off.
//
// The Limiter runs whether on or off. On and off crossfade between its output and its input, which have the same
// delay, with plugkit's Smoother; settled off, the output is its input, bit for bit. The Ceiling glides on plugkit's
// Smoother too, in linear gain, so it never passes the higher of the old and new Ceilings. Below the Ceiling the
// Gain Reduction settles on exactly none, and the audio passes bit for bit. Each sample moves the same however the
// audio is cut. When a Processing Mode's path starts playing again, its Limiter starts afresh.

#include "Bessel.h"
#include "Glide.h"
#include "comp1/Engine.h"
#include "comp1/Latency.h"

#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace comp1
{

class Limiter
{
public:
    // dyn-lab's figures: LIMITER_RELEASE_MS, LIMITER_TRUE_PEAK_HEADROOM_DB and LIMITER_SETTLED_DB.
    static constexpr double releaseMs = 200.0;
    static constexpr double truePeakHeadroomDb = 0.3;
    static constexpr double settledDb = 1.0e-7;
    // The true-peak estimator, dyn-lab's TRUE_PEAK_FACTOR, TRUE_PEAK_TAPS and TRUE_PEAK_BETA: it delays by half its
    // length at every sample rate.
    static constexpr int estimatorFactor = 8;
    static constexpr int estimatorTaps = 48;
    static constexpr double estimatorBeta = 6.0;
    static constexpr int estimatorDelay = estimatorTaps / 2;
    // The Ceiling glides this many times plugkit's glide time. Zero Latency's zero attack follows a falling Ceiling
    // at once, a step down at each peak, and at plugkit's glide time those steps are corners the click test hears.
    static constexpr double ceilingGlides = 4.0;

    Limiter()
        : headroomGain (plugkit::decibelsToGain (-truePeakHeadroomDb))
    {
        designEstimator();
    }

    // Allocates for numChannels channels at sampleRate, and starts both paths afresh, off, at a Ceiling of 0 dBFS.
    void prepare (double sampleRate, int numChannels)
    {
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        releasePole = std::exp (-1000.0 / (releaseMs * sampleRate));
        // The whole Limiter reserve, the estimator's delay and the window.
        reserve = limiterReserveSamples (sampleRate);
        span = std::max (reserve - estimatorDelay, 0) + 1;
        // Before the Limiter, High Quality's other delays after the compressor hold what the path last played; when it
        // starts again, the estimator hears silence in their place (startPath).
        staleSamples = latencySamples (ProcessingMode::HighQuality, sampleRate) - lookaheadBudgetSamples (sampleRate) - reserve;
        history.assign (channels, {});
        delayed.assign (channels, std::vector<float> (static_cast<std::size_t> (reserve)));
        heldValues.assign (static_cast<std::size_t> (span), 0.0);
        heldPositions.assign (static_cast<std::size_t> (span), 0);
        averaged.assign (static_cast<std::size_t> (span), 0.0);
        constexpr double glide = plugkit::glideTimeConstantSeconds;
        onShare.configure (glide * sampleRate, 1.0e-6);
        onShare.reset (0.0);
        ceiling.configure (ceilingGlides * glide * sampleRate, 1.0e-9);
        ceiling.reset (1.0);
        startPath (ProcessingMode::ZeroLatency);
        startPath (ProcessingMode::HighQuality);
        playing = ProcessingMode::ZeroLatency;
        position = 0;
        for (auto& channel : delayed)
            std::fill (channel.begin(), channel.end(), 0.0f);
    }

    // With snap, each takes effect at once instead of gliding.
    void setOn (bool on, bool snap) { glideTo (onShare, on ? 1.0 : 0.0, snap); }

    void setCeiling (double ceilingDb, bool snap) { glideTo (ceiling, plugkit::decibelsToGain (ceilingDb), snap); }

    // Limits block in place, on path: High Quality's delays it by the Limiter reserve.
    void process (AudioBlock block, ProcessingMode path)
    {
        if (path != playing)
        {
            playing = path;
            startPath (path);
        }
        for (int i = 0; i < block.numSamples; ++i)
        {
            const double share = onShare.isSettled() ? onShare.value() : onShare.next();
            const double ceilingGain = ceiling.isSettled() ? ceiling.value() : ceiling.next();
            if (path == ProcessingMode::HighQuality)
                highQualitySample (block, i, share, ceilingGain);
            else
                zeroLatencySample (block, i, share, ceilingGain);
        }
    }

private:
    // --- Zero Latency ---

    void zeroLatencySample (AudioBlock block, int i, double share, double ceilingGain)
    {
        double peak = 0.0;
        for (int ch = 0; ch < block.numChannels; ++ch)
            peak = std::max (peak, static_cast<double> (std::abs (block.channels[ch][i])));
        zeroLatencyReduction = settled (std::max (overDb (peak, ceilingGain), releasePole * zeroLatencyReduction));
        if (share == 0.0)
            return;
        const double gain = gainOf (zeroLatencyReduction);
        // The largest float at or under the Ceiling, so a clipped sample never rounds past it.
        auto clip = static_cast<float> (ceilingGain);
        if (static_cast<double> (clip) > ceilingGain)
            clip = std::nextafter (clip, 0.0f);
        for (int ch = 0; ch < block.numChannels; ++ch)
        {
            float& sample = block.channels[ch][i];
            const float limited = std::clamp (static_cast<float> (gain * sample), -clip, clip);
            if (share == 1.0)
                sample = limited;
            else if (share != 0.0)
                sample = static_cast<float> (share * limited + (1.0 - share) * sample);
        }
    }

    // --- High Quality ---

    void highQualitySample (AudioBlock block, int i, double share, double ceilingGain)
    {
        // The estimate for the sample estimatorDelay ago: the loudest of its segment and the one before, on every
        // channel. While stale audio arrives, silence.
        double segment = 0.0;
        const double aim = ceilingGain * headroomGain;
        const bool stale = staleLeft > 0;
        if (stale)
            --staleLeft;
        for (int ch = 0; ch < block.numChannels; ++ch)
        {
            auto& h = history[static_cast<std::size_t> (ch)];
            const double sample = stale ? 0.0 : static_cast<double> (block.channels[ch][i]);
            // Written twice, so the latest estimatorTaps samples sit in a row from historyPosition + 1.
            h[static_cast<std::size_t> (historyPosition)] = h[static_cast<std::size_t> (historyPosition + estimatorTaps)] = sample;
            segment = std::max (segment, segmentPeak (h.data() + historyPosition + 1, aim));
        }
        historyPosition = historyPosition + 1 == estimatorTaps ? 0 : historyPosition + 1;
        const double peak = std::max (segment, previousSegment);
        previousSegment = segment;
        pushHeld (overDb (peak, aim));
        released = settled (std::max (heldValues[static_cast<std::size_t> (heldFront)], releasePole * released));
        pushAveraged (released);
        const double reduction = nonzeroAveraged > 0 ? averagedSum / span : 0.0;

        // The audio, delayed by the whole reserve, takes the gain: the crossfade between it limited and as it is,
        // the same scale, is a crossfade of the gain.
        const double gain = share == 0.0 ? 1.0 : share * gainOf (reduction) + (1.0 - share);
        for (int ch = 0; ch < block.numChannels; ++ch)
        {
            float& slot = delayed[static_cast<std::size_t> (ch)][static_cast<std::size_t> (position)];
            const float out = slot;
            slot = block.channels[ch][i];
            block.channels[ch][i] = gain == 1.0 ? out : static_cast<float> (gain * out);
        }
        position = position + 1 == reserve ? 0 : position + 1;
    }

    // The loudest of the sample at the middle of the estimatorTaps samples from window and the estimator's points
    // between it and the next. Where no phase can reach aim, the most any could read instead, which asks for the same
    // Gain Reduction, none, for less work.
    double segmentPeak (const double* window, double aim) const
    {
        double loudest = 0.0;
        for (int j = 0; j < estimatorTaps; ++j)
            loudest = std::max (loudest, std::abs (window[j]));
        if (loudest * mostGain <= aim)
            return loudest * mostGain;
        double peak = std::abs (window[estimatorDelay - 1]);
        for (const auto& phase : phases)
        {
            double sum = 0.0;
            for (int j = 0; j < estimatorTaps; ++j)
                sum += phase[static_cast<std::size_t> (j)] * window[j];
            peak = std::max (peak, std::abs (sum));
        }
        return peak;
    }

    // The sliding maximum over the last span values asked for: a deque, largest at the front, of values with where
    // they came, in rings of span.
    void pushHeld (double value)
    {
        if (heldCount > 0 && heldPositions[static_cast<std::size_t> (heldFront)] <= askedCount - span)
        {
            heldFront = (heldFront + 1) % span;
            --heldCount;
        }
        while (heldCount > 0 && heldValues[static_cast<std::size_t> (back())] <= value)
            --heldCount;
        const int slot = (heldFront + heldCount) % span;
        heldValues[static_cast<std::size_t> (slot)] = value;
        heldPositions[static_cast<std::size_t> (slot)] = askedCount;
        ++heldCount;
        ++askedCount;
    }

    int back() const { return (heldFront + heldCount - 1) % span; }

    // The average's ring: its sum, and how many of its values aren't 0, so it reads exactly 0 once they all are.
    void pushAveraged (double value)
    {
        auto& slot = averaged[static_cast<std::size_t> (averagedPosition)];
        nonzeroAveraged += (value != 0.0) - (slot != 0.0);
        averagedSum += value - slot;
        slot = value;
        averagedPosition = averagedPosition + 1 == span ? 0 : averagedPosition + 1;
        if (nonzeroAveraged == 0)
            averagedSum = 0.0;
    }

    // --- Both ---

    // The Gain Reduction in dB that brings peak to ceilingGain: 0 when it is under it.
    static double overDb (double peak, double ceilingGain) { return peak > ceilingGain ? plugkit::gainToDecibels (peak / ceilingGain) : 0.0; }

    static double settled (double reductionDb) { return reductionDb < settledDb ? 0.0 : reductionDb; }

    static double gainOf (double reductionDb) { return reductionDb > 0.0 ? plugkit::decibelsToGain (-reductionDb) : 1.0; }

    // path starts afresh, with no Gain Reduction; High Quality's estimator hears silence for the stale samples.
    void startPath (ProcessingMode path)
    {
        if (path == ProcessingMode::ZeroLatency)
        {
            zeroLatencyReduction = 0.0;
            return;
        }
        for (auto& h : history)
            h.fill (0.0);
        historyPosition = 0;
        previousSegment = 0.0;
        std::fill (heldValues.begin(), heldValues.end(), 0.0);
        heldFront = heldCount = 0;
        askedCount = 0;
        released = 0.0;
        std::fill (averaged.begin(), averaged.end(), 0.0);
        averagedSum = 0.0;
        nonzeroAveraged = 0;
        averagedPosition = 0;
        staleLeft = staleSamples;
    }

    // Each phase between samples: a Kaiser-windowed sinc, its cutoff the host's Nyquist, normalised to unity at DC.
    void designEstimator()
    {
        constexpr int half = estimatorTaps / 2;
        for (int p = 1; p < estimatorFactor; ++p)
        {
            auto& phase = phases[static_cast<std::size_t> (p - 1)];
            double total = 0.0;
            for (int j = 0; j < estimatorTaps; ++j)
            {
                const double t = static_cast<double> (p) / estimatorFactor - (j + 1 - half);
                const double u = t / half;
                const double window = besselI0 (estimatorBeta * std::sqrt (std::max (0.0, 1.0 - u * u))) / besselI0 (estimatorBeta);
                phase[static_cast<std::size_t> (j)] = std::sin (std::numbers::pi * t) / (std::numbers::pi * t) * window;
                total += phase[static_cast<std::size_t> (j)];
            }
            double gain = 0.0;
            for (double& c : phase)
            {
                c /= total;
                gain += std::abs (c);
            }
            mostGain = std::max (mostGain, gain);
        }
    }

    std::array<std::array<double, estimatorTaps>, estimatorFactor - 1> phases {};
    double headroomGain = 1.0; // the true peak High Quality aims for, against the Ceiling
    double mostGain = 1.0;     // the most any phase reads of samples up to 1 in magnitude, its taps' magnitudes summed
    double releasePole = 0.0;
    int reserve = 1, span = 1, staleSamples = 0;
    plugkit::Smoother onShare; // 0 off, 1 on
    plugkit::Smoother ceiling; // linear gain
    ProcessingMode playing = ProcessingMode::ZeroLatency;

    double zeroLatencyReduction = 0.0; // dB

    std::vector<std::array<double, 2 * estimatorTaps>> history; // per channel, the latest samples, written twice
    int historyPosition = 0;
    double previousSegment = 0.0;
    int staleLeft = 0;
    std::vector<double> heldValues;
    std::vector<std::int64_t> heldPositions;
    int heldFront = 0, heldCount = 0;
    std::int64_t askedCount = 0; // how many Gain Reductions have been asked for since the path started
    double released = 0.0; // dB
    std::vector<double> averaged;
    double averagedSum = 0.0;
    int nonzeroAveraged = 0, averagedPosition = 0;
    std::vector<std::vector<float>> delayed; // per channel, a ring of the reserve's length
    int position = 0;
};

} // namespace comp1
