#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

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
