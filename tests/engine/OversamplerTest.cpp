#include "Measure.h"
#include "Oversampler.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

using namespace comp1;
using oversampling::Oversampler;

namespace
{

// One factor's filters as python3 tools/dyn-lab/dynlab.py oversampling prints them, copied as printed: each stage's
// taps, the up and down filters' delay in host samples, the internal padding, and the stopband in dB. The designs are
// fractions of the rate, so every rate that allows a factor prints the same row for it.
struct Filters
{
    Oversampling oversampling;
    std::vector<int> taps;
    int delay, internalPadding;
    double stopbandDb;
};

const std::vector<Filters>& labFilters()
{
    static const std::vector<Filters> rows {
        { Oversampling::x2, { 67 }, 33, 0, 100.17 },
        { Oversampling::x4, { 67, 27 }, 40, 2, 100.20 },
        { Oversampling::x8, { 67, 27, 23 }, 43, 6, 100.20 },
    };
    return rows;
}

// Each sample rate comp1 supports with each factor it allows there, past 1x.
struct Allowed
{
    double sampleRate;
    Filters filters;
};

std::vector<Allowed> allowed()
{
    std::vector<Allowed> all;
    for (const double sampleRate : test::sampleRates())
        for (const auto& filters : labFilters())
            if (filters.oversampling <= maxOversampling (sampleRate))
                all.push_back ({ sampleRate, filters });
    return all;
}

// dyn-lab's OVERSAMPLING_PASSBAND_EDGE: the passband edge as a fraction of the host's Nyquist.
constexpr double passbandEdge = 0.8;
// How much of the lab's stopband the Engine's may fall short by: its taps are the lab's design worked out in the same
// double arithmetic, so they differ only by rounding, far under this.
constexpr double stopbandWithinDb = 0.01;

// The magnitude at f, in host rates, of the cascade's up filters, from their impulse response at the oversampled rate,
// over their gain of factor (zero-stuffing a sample into factor leaves a factor's share of its level).
double upGain (const std::vector<double>& impulseResponse, int factor, double f)
{
    std::complex<double> sum = 0.0;
    for (std::size_t n = 0; n < impulseResponse.size(); ++n)
        sum += impulseResponse[n] * std::polar (1.0, -2.0 * std::numbers::pi * f * static_cast<double> (n) / factor);
    return std::abs (sum) / factor;
}

} // namespace

TEST_CASE ("Each Oversampling's filters have dyn-lab's taps and delay, at every rate that allows it")
{
    const auto row = GENERATE (from_range (allowed()));
    CAPTURE (row.sampleRate, factorOf (row.filters.oversampling));
    const auto& filters = row.filters;
    REQUIRE (oversampling::stagesOf (filters.oversampling) == static_cast<int> (filters.taps.size()));
    for (std::size_t stage = 0; stage < filters.taps.size(); ++stage)
    {
        const auto& taps = oversampling::stageTaps (static_cast<int> (stage));
        CHECK (static_cast<int> (taps.size()) == filters.taps[stage]);
        // A half-band: symmetric, its centre 1/2 and every other tap beside it exactly 0.
        const std::size_t centre = (taps.size() - 1) / 2;
        CHECK (taps[centre] == 0.5);
        for (std::size_t j = 2; j <= centre; j += 2)
            CHECK (taps[centre + j] == 0.0);
        CHECK (std::equal (taps.begin(), taps.end(), taps.rbegin()));
    }
    CHECK (oversampling::pairDelaySamples (filters.oversampling) == filters.delay);
    CHECK (oversampling::internalPaddingSamples (filters.oversampling) == filters.internalPadding);
    CHECK (filters.delay <= oversamplingSectionSamples);
}

TEST_CASE ("At 1x the filters pass the audio as it is, with no delay")
{
    CHECK (oversampling::pairDelaySamples (Oversampling::x1) == 0);
    Oversampler filters;
    filters.prepare (Oversampling::x8, 16);
    const auto input = test::noise (1, 16).front();
    std::vector<double> up (16), down (16);
    filters.up (input.data(), 16, up.data());
    filters.down (up.data(), 16, down.data());
    for (std::size_t i = 0; i < input.size(); ++i)
        CHECK (down[i] == static_cast<double> (input[i]));
}

TEST_CASE ("The up filters' stopband is within dyn-lab's figure, at every factor")
{
    const auto& filters = GENERATE (from_range (labFilters()));
    const int factor = factorOf (filters.oversampling);
    CAPTURE (factor);
    Oversampler oversampler;
    oversampler.prepare (filters.oversampling, 16);
    oversampler.setOversampling (filters.oversampling);
    // The impulse response: an impulse, then silence, long enough for every stage to ring out.
    std::vector<double> response;
    std::vector<double> piece (static_cast<std::size_t> (16 * factor));
    for (int block = 0; block < 8; ++block)
    {
        std::vector<float> host (16, 0.0f);
        if (block == 0)
            host[0] = 1.0f;
        oversampler.up (host.data(), 16, piece.data());
        response.insert (response.end(), piece.begin(), piece.end());
    }
    // dyn-lab's stopband: within the passband edge of each multiple of the host rate, up to the oversampled Nyquist.
    const double edge = passbandEdge / 2.0;
    double leastDb = 1000.0;
    for (int k = 1; k <= factor / 2; ++k)
    {
        const double start = k - edge, end = std::min (k + edge, factor / 2.0);
        constexpr int points = 400;
        for (int i = 0; i <= points; ++i)
            leastDb = std::min (leastDb, -20.0 * std::log10 (upGain (response, factor, start + (end - start) * i / points)));
    }
    CHECK (leastDb >= filters.stopbandDb - stopbandWithinDb);
    // And the passband is flat: a factor's share of an impulse at DC comes through whole.
    CHECK (std::abs (20.0 * std::log10 (upGain (response, factor, 0.0))) < 1.0e-3);
}

TEST_CASE ("The down filters stop what would fold into the passband, within dyn-lab's figure, at every factor")
{
    const auto& filters = GENERATE (from_range (labFilters()));
    const int factor = factorOf (filters.oversampling);
    CAPTURE (factor);
    // A tone at the oversampled rate in each band that folds into the host's passband, at its middle and edges.
    const double edge = passbandEdge / 2.0;
    for (int k = 1; k <= factor / 2; ++k)
        for (const double f : { k - edge, static_cast<double> (k), std::min (k + edge, factor / 2.0 - 1.0e-3) })
        {
            CAPTURE (f);
            Oversampler oversampler;
            oversampler.prepare (filters.oversampling, 64);
            oversampler.setOversampling (filters.oversampling);
            constexpr int blocks = 16;
            std::vector<double> in (static_cast<std::size_t> (64 * factor)), out (64);
            double peak = 0.0;
            for (int block = 0, n = 0; block < blocks; ++block)
            {
                for (auto& sample : in)
                    sample = std::sin (2.0 * std::numbers::pi * f * n++ / factor);
                oversampler.down (in.data(), 64, out.data());
                if (block >= 4)
                    for (const double sample : out)
                        peak = std::max (peak, std::abs (sample));
            }
            CHECK (-20.0 * std::log10 (peak) >= filters.stopbandDb - stopbandWithinDb);
        }
}

TEST_CASE ("Up then down passes the passband delayed by the pair's delay, the same however the audio is cut")
{
    const auto& filters = GENERATE (from_range (labFilters()));
    const int factor = factorOf (filters.oversampling);
    CAPTURE (factor);
    // A tone well inside the passband edge: 3 kHz at 44.1 kHz.
    std::vector<float> input (2000);
    for (std::size_t n = 0; n < input.size(); ++n)
        input[n] = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 3000.0 * static_cast<double> (n) / 44100.0));

    const auto through = [&] (const std::vector<int>& cut) {
        Oversampler oversampler;
        oversampler.prepare (Oversampling::x8, 16);
        oversampler.setOversampling (filters.oversampling);
        std::vector<double> out, up (static_cast<std::size_t> (16 * factor)), down (16);
        for (std::size_t start = 0, c = 0; start < input.size(); ++c)
        {
            const int count = std::min (cut[c % cut.size()], static_cast<int> (input.size() - start));
            oversampler.up (input.data() + start, count, up.data());
            oversampler.down (up.data(), count, down.data());
            out.insert (out.end(), down.begin(), down.begin() + count);
            start += static_cast<std::size_t> (count);
        }
        return out;
    };
    const auto whole = through ({ 16 });
    const int delay = filters.delay;
    double worst = 0.0;
    for (std::size_t n = 500; n < input.size(); ++n)
        worst = std::max (worst, std::abs (whole[n] - static_cast<double> (input[n - static_cast<std::size_t> (delay)])));
    // Within the passband ripple, 10^(-100/20) a stage each way.
    CHECK (worst < 1.0e-4);
    CHECK (through ({ 1, 7, 16, 3 }) == whole);
}
