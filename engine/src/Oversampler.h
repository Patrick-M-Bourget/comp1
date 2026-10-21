#pragma once

// Oversampling's filters, as dyn-lab designs them (python3 tools/dyn-lab/dynlab.py oversampling, and USAGE.md's
// "oversampling"): each 2x stage a linear-phase Kaiser-windowed half-band FIR, cascaded for 4x and 8x, up and down
// with the same design. Only a nonlinear stage's residual passes through them (ADR 0009), so their passband edge sits
// at 0.8 of the host's Nyquist.

#include "Bessel.h"
#include "comp1/Latency.h"
#include "comp1/Settings.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace comp1::oversampling
{

// The stages a cascade can hold: 8x's three.
inline constexpr int maxStages = 3;

// Each stage's length in taps, the first (host rate to 2x) first: dyn-lab's, the shortest half-band, 4m - 1 taps,
// whose stopband reaches OVERSAMPLING_STOPBAND_DB with the Kaiser window below.
inline constexpr std::array<int, maxStages> stageLengths { 67, 27, 23 };

// The Kaiser window's beta: dyn-lab's KAISER_BETA, 0.1102 (A - 8.7) for its 100 dB stopband.
inline constexpr double kaiserBeta = 0.1102 * (100.0 - 8.7);

// How many 2x stages oversampling cascades.
inline constexpr int stagesOf (Oversampling oversampling) { return static_cast<int> (oversampling); }

// A Kaiser-windowed half-band lowpass of length taps (4m - 1), its gain 1 at DC: the centre tap 1/2, every other tap
// on each side exactly 0, the rest the ideal half-band's sin(πn/2)/(πn), windowed.
inline std::vector<double> halfBand (int length)
{
    const int centre = (length - 1) / 2;
    const double window = besselI0 (kaiserBeta);
    std::vector<double> taps (static_cast<std::size_t> (length), 0.0);
    for (int n = 0; n < length; ++n)
    {
        const int offset = n - centre;
        if (offset == 0)
            taps[static_cast<std::size_t> (n)] = 0.5;
        else if (offset % 2 != 0)
        {
            const double ratio = static_cast<double> (offset) / centre;
            taps[static_cast<std::size_t> (n)] = std::sin (std::numbers::pi * offset / 2.0) / (std::numbers::pi * offset)
                                                 * besselI0 (kaiserBeta * std::sqrt (1.0 - ratio * ratio)) / window;
        }
    }
    return taps;
}

// Stage stage's half-band, designed once.
inline const std::vector<double>& stageTaps (int stage)
{
    static const std::array<std::vector<double>, maxStages> designs { halfBand (stageLengths[0]), halfBand (stageLengths[1]),
                                                                      halfBand (stageLengths[2]) };
    return designs[static_cast<std::size_t> (stage)];
}

// The up and down filters' delay together, in samples at the oversampled rate: a stage of N taps delays by (N - 1) / 2
// samples at its own rate each way, and stage s runs at 2^(s + 1) host rates.
inline int filterDelayOversampled (Oversampling oversampling)
{
    const int factor = factorOf (oversampling);
    int total = 0;
    for (int stage = 0; stage < stagesOf (oversampling); ++stage)
        total += (stageLengths[static_cast<std::size_t> (stage)] - 1) * factor / (2 << stage);
    return total;
}

// The up and down filters' delay together in whole host samples, rounded up: dyn-lab's pair delay.
inline int pairDelaySamples (Oversampling oversampling)
{
    const int factor = factorOf (oversampling);
    return (filterDelayOversampled (oversampling) + factor - 1) / factor;
}

// The padding, in samples at the oversampled rate, that rounds the filters' delay up to whole host samples.
inline int internalPaddingSamples (Oversampling oversampling)
{
    return pairDelaySamples (oversampling) * factorOf (oversampling) - filterDelayOversampled (oversampling);
}

// One 2x half-band stage, one way, and the inputs it still reads. Never allocates after prepare().
class HalfBandStage
{
public:
    // Allocates for up to maxInput samples a call, and clears the history.
    void prepare (const std::vector<double>& design, int maxInput)
    {
        taps = &design;
        length = static_cast<int> (design.size());
        buffer.assign (static_cast<std::size_t> (length - 1 + std::max (maxInput, 0)), 0.0);
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0); }

    // count samples of in up to twice as many in out: zero-stuffed, then filtered at twice the taps' gain, as a
    // polyphase pair. Each even output sums the taps at odd offsets from the centre; each odd one is the centre tap's
    // alone, which at twice its gain is an input itself. Delayed by (length - 1) / 2 samples at the output rate.
    void up (const double* in, int count, double* out)
    {
        const double* x = take (in, count);
        const int centre = (length - 1) / 2;
        const auto& h = *taps;
        for (int i = 0; i < count; ++i)
        {
            const double* newest = x + i; // input i; newest[-j] is input i - j
            double sum = 0.0;
            for (int j = 0; j <= centre; ++j)
                sum += h[static_cast<std::size_t> (2 * j)] * newest[-j];
            out[2 * i] = 2.0 * sum;
            out[2 * i + 1] = newest[-(centre - 1) / 2];
        }
        keep (count);
    }

    // count samples of in (an even count) down to half as many in out: filtered, keeping every other sample, as a
    // polyphase pair. Delayed by (length - 1) / 2 samples at the input rate.
    void down (const double* in, int count, double* out)
    {
        const double* u = take (in, count);
        const int centre = (length - 1) / 2;
        const auto& h = *taps;
        for (int k = 0; k < count / 2; ++k)
        {
            const double* newest = u + 2 * k; // input 2k; newest[-i] is input 2k - i
            double sum = 0.0;
            for (int j = 0; j <= centre; ++j)
                sum += h[static_cast<std::size_t> (2 * j)] * newest[-2 * j];
            out[k] = sum + 0.5 * newest[-centre];
        }
        keep (count);
    }

private:
    // Appends count samples of in after the history, and returns where the first of them is.
    const double* take (const double* in, int count)
    {
        double* first = buffer.data() + length - 1;
        std::copy (in, in + count, first);
        return first;
    }

    // Keeps the last length - 1 samples as the history for the next call.
    void keep (int count)
    {
        std::copy (buffer.begin() + count, buffer.begin() + count + length - 1, buffer.begin());
    }

    const std::vector<double>* taps = nullptr;
    int length = 1;
    std::vector<double> buffer; // the history, then the samples of the call
};

// One channel's cascade of up and down filters at one Oversampling, sized at prepare() for the largest, so up() and
// down() never allocate. up() takes host samples and writes factor times as many at the oversampled rate, delayed by
// the up filters; down() takes those back and writes host samples, delayed by the down filters and the internal
// padding. Up then down delays by exactly pairDelaySamples() host samples.
class Oversampler
{
public:
    // Allocates for up to largest, with up to maxHostSamples host samples a call, and clears the history.
    void prepare (Oversampling largest, int maxHostSamples)
    {
        maxSamples = std::max (maxHostSamples, 0);
        const int stages = stagesOf (largest);
        for (int stage = 0; stage < stages; ++stage)
        {
            const auto s = static_cast<std::size_t> (stage);
            upStages[s].prepare (stageTaps (stage), maxSamples << stage);
            downStages[s].prepare (stageTaps (stage), maxSamples << (stage + 1));
        }
        const auto oversampled = static_cast<std::size_t> (maxSamples * factorOf (largest));
        for (auto& buffer : buffers)
            buffer.assign (oversampled, 0.0);
        padded.assign (oversampled + static_cast<std::size_t> (factorOf (largest)), 0.0);
        prepared = largest;
        setOversampling (Oversampling::x1);
    }

    // Takes oversampling, at most the prepared largest, and clears the history.
    void setOversampling (Oversampling newOversampling)
    {
        assert (newOversampling <= prepared);
        oversampling = newOversampling;
        reset();
    }

    Oversampling getOversampling() const { return oversampling; }

    // Clears the history, as if every sample before were 0.
    void reset()
    {
        for (int stage = 0; stage < stagesOf (prepared); ++stage)
        {
            upStages[static_cast<std::size_t> (stage)].reset();
            downStages[static_cast<std::size_t> (stage)].reset();
        }
        std::fill (padded.begin(), padded.end(), 0.0);
    }

    // count host samples of in, up to factor times as many in out.
    template <typename Sample>
    void up (const Sample* in, int count, double* out)
    {
        assert (count <= maxSamples);
        const int stages = stagesOf (oversampling);
        if (stages == 0)
        {
            std::copy (in, in + count, out);
            return;
        }
        std::copy (in, in + count, buffers[0].begin());
        const double* from = buffers[0].data();
        for (int stage = 0; stage < stages; ++stage)
        {
            double* to = stage == stages - 1 ? out : (from == buffers[0].data() ? buffers[1].data() : buffers[0].data());
            upStages[static_cast<std::size_t> (stage)].up (from, count << stage, to);
            from = to;
        }
    }

    // factor times count samples of in, at the oversampled rate, down to count host samples in out. in and out may
    // not be the same.
    void down (const double* in, int count, double* out)
    {
        assert (count <= maxSamples);
        const int stages = stagesOf (oversampling);
        if (stages == 0)
        {
            std::copy (in, in + count, out);
            return;
        }
        // The internal padding first, at the oversampled rate: padded holds its delay's samples, then in's.
        const int pad = internalPaddingSamples (oversampling);
        const int total = count * factorOf (oversampling);
        std::copy (in, in + total, padded.begin() + pad);
        const double* from = padded.data();
        for (int stage = stages - 1; stage >= 0; --stage)
        {
            double* to = stage == 0 ? out : (from == buffers[0].data() ? buffers[1].data() : buffers[0].data());
            downStages[static_cast<std::size_t> (stage)].down (from, count << (stage + 1), to);
            from = to;
        }
        std::copy (padded.begin() + total, padded.begin() + total + pad, padded.begin());
    }

private:
    Oversampling prepared = Oversampling::x1, oversampling = Oversampling::x1;
    int maxSamples = 0;
    std::array<HalfBandStage, maxStages> upStages, downStages;
    std::array<std::vector<double>, 2> buffers; // between the stages, at up to the oversampled rate
    std::vector<double> padded;                 // the internal padding's delay, then the samples of a call
};

} // namespace comp1::oversampling
