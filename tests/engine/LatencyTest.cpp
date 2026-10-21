#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <utility>
#include <vector>

using namespace comp1;

namespace
{

// High Quality's latency at one sample rate and its three parts, in host samples.
struct Row
{
    double sampleRate;
    int highQuality, oversampling, lookahead, limiter;
};

// The output of python3 tools/dyn-lab/dynlab.py latency, copied as printed (Zero Latency's column is 0 throughout).
const std::vector<Row>& table()
{
    static const std::vector<Row> rows {
        { 44100, 394, 128, 221, 45 },  { 48000, 416, 128, 240, 48 },     { 88200, 658, 128, 441, 89 },
        { 96000, 704, 128, 480, 96 },  { 176400, 1187, 128, 882, 177 }, { 192000, 1280, 128, 960, 192 },
    };
    return rows;
}

} // namespace

TEST_CASE ("The latency table covers every sample rate comp1 supports")
{
    std::vector<double> rates;
    for (const auto& row : table())
        rates.push_back (row.sampleRate);
    CHECK (rates == test::sampleRates());
}

TEST_CASE ("Zero Latency reports 0, and High Quality dyn-lab's fixed latency, at every sample rate")
{
    const auto& row = GENERATE (from_range (table()));
    CAPTURE (row.sampleRate);
    CHECK (latencySamples (ProcessingMode::ZeroLatency, row.sampleRate) == 0);
    CHECK (latencySamples (ProcessingMode::HighQuality, row.sampleRate) == row.highQuality);
    CHECK (oversamplingReserveSamples == row.oversampling);
    CHECK (lookaheadBudgetSamples (row.sampleRate) == row.lookahead);
    CHECK (limiterReserveSamples (row.sampleRate) == row.limiter);
}

TEST_CASE ("The Wet path's delay up to the Dry/Wet sum is the Oversampling reserve and the Lookahead budget")
{
    const auto& row = GENERATE (from_range (table()));
    CAPTURE (row.sampleRate);
    CHECK (wetPathLatencySamples (ProcessingMode::ZeroLatency, row.sampleRate) == 0);
    CHECK (wetPathLatencySamples (ProcessingMode::HighQuality, row.sampleRate) == row.oversampling + row.lookahead);
}

namespace
{

// The largest Oversampling allowed at each sample rate, from python3 tools/dyn-lab/dynlab.py oversampling, copied as
// printed: the internal rate at or under 384 kHz (ADR 0001).
const std::vector<std::pair<double, Oversampling>>& oversamplingCaps()
{
    static const std::vector<std::pair<double, Oversampling>> rows {
        { 44100, Oversampling::x8 }, { 48000, Oversampling::x8 },   { 88200, Oversampling::x4 },
        { 96000, Oversampling::x4 }, { 176400, Oversampling::x2 }, { 192000, Oversampling::x2 },
    };
    return rows;
}

} // namespace

TEST_CASE ("The largest Oversampling allowed is dyn-lab's at every sample rate")
{
    std::vector<double> rates;
    for (const auto& [sampleRate, cap] : oversamplingCaps())
    {
        CAPTURE (sampleRate);
        CHECK (maxOversampling (sampleRate) == cap);
        rates.push_back (sampleRate);
    }
    CHECK (rates == test::sampleRates());
}

TEST_CASE ("Each Oversampling section's delay is half the Oversampling reserve")
{
    // dyn-lab's OVERSAMPLING_SECTION_SAMPLES: the two sections together take the whole reserve.
    CHECK (oversamplingSectionSamples == 64);
    CHECK (2 * oversamplingSectionSamples == oversamplingReserveSamples);
}

TEST_CASE ("With every stage on, High Quality's impulse comes out at the reported latency, at every sample rate", "[sweep]")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (sampleRate, static_cast<int> (position));
    // Ratio 1:1, so the compressor leaves the impulse whole; every other stage on and working: the Gate closed on the
    // silence before it, Saturation driven hard at 8x (or the sample rate's cap), VCA's Colour, Lookahead's whole
    // budget, the Limiter pulling the impulse, 0.5, down to its Ceiling, and Dry summed with Wet.
    const Settings settings { .gate = { .on = true, .thresholdDb = -30.0, .rangeDb = 6.0 },
                              .character = Character::VCA,
                              .ratio = 1.0,
                              .saturation = true,
                              .driveDb = 24.0,
                              .saturationPosition = position,
                              .dryDb = 0.0,
                              .limiter = true,
                              .ceilingDb = -12.0,
                              .processingMode = ProcessingMode::HighQuality,
                              .lookaheadMs = parameterRanges::lookahead.high,
                              .oversampling = Oversampling::x8 };
    CHECK (test::impulseDelaySamples (settings, sampleRate) == latencySamples (ProcessingMode::HighQuality, sampleRate));
}
