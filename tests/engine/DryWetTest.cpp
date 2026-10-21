#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/test/AllocationGuard.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;
constexpr double off = -std::numeric_limits<double>::infinity();

// The settings the null must hold under, one row each. Oversampling (#10) adds a row per factor, Colour (#19) a row
// per Colour.
const std::vector<test::NullRow>& nullRows()
{
    static const std::vector<test::NullRow> rows {
        { "Clean", [] (Settings&) {} },
        // With no Gain Reduction, Colour's weight is exactly 0, at any Colour (ADR 0005).
        { "VCA at Colour 100%", [] (Settings& s) { s.character = Character::VCA; } },
        { "VCA at Colour 0%", [] (Settings& s) {
             s.character = Character::VCA;
             s.colour = 0.0;
         } },
    };
    return rows;
}

// A pair of Dry and Wet levels, and the gain the input then comes out at, worked out independently of the Engine
// (10^(dB/20)): exactly, bit for bit, or within a relative error of 1e-6.
struct Levels
{
    double dryDb, wetDb, gain;
    bool exact;
};

const std::vector<Levels>& nullLevels()
{
    static const std::vector<Levels> levels {
        { off, 0.0, 1.0, true },
        { 0.0, off, 1.0, true },
        { 0.0, 0.0, 2.0, true },
        { -6.0, -12.0, 0.501187233627272 + 0.251188643150958, false },
        { 12.0, -24.0, 3.98107170553497 + 0.0630957344480193, false },
        { -3.0, 6.0, 0.707945784384138 + 1.99526231496888, false },
    };
    return levels;
}

} // namespace

TEST_CASE ("With no compression, Dry plus Wet nulls against the input delayed by the reported latency")
{
    const auto& row = GENERATE (from_range (nullRows()));
    const auto& levels = GENERATE (from_range (nullLevels()));
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    const double lookaheadMs = GENERATE (0.0, 2.5, 5.0);
    CAPTURE (row.name, levels.dryDb, levels.wetDb, static_cast<int> (mode), sampleRate, cut, lookaheadMs);

    const auto input = test::noise (2, 6000);
    const auto output = test::played (test::nullSettings (row, mode, lookaheadMs, levels.dryDb, levels.wetDb), sampleRate, input, cut);
    const auto expected = test::delayed (input, latencySamples (mode, sampleRate));
    if (levels.exact)
        CHECK (test::worstRelativeError (output, expected, levels.gain) == 0.0);
    else
        CHECK (test::worstRelativeError (output, expected, levels.gain) < 1.0e-6);
}

TEST_CASE ("An impulse on the Dry path alone and on the Wet path alone comes out at the same sample, the reported latency")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const double lookaheadMs = GENERATE (0.0, 5.0);
    CAPTURE (static_cast<int> (mode), sampleRate, lookaheadMs);
    const auto& clean = nullRows().front();
    const int dryAlone = test::impulseDelaySamples (test::nullSettings (clean, mode, lookaheadMs, 0.0, off), sampleRate, 17);
    const int wetAlone = test::impulseDelaySamples (test::nullSettings (clean, mode, lookaheadMs, off, 0.0), sampleRate, 17);
    CHECK (dryAlone == latencySamples (mode, sampleRate));
    CHECK (wetAlone == dryAlone);
}

TEST_CASE ("At Dry 0 dB and Wet Off, compression, Saturation and Auto Gain change nothing, bit for bit")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (48000.0, 192000.0);
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    const double lookaheadMs = GENERATE (0.0, 5.0);
    CAPTURE (static_cast<int> (mode), sampleRate, cut, lookaheadMs);
    // Noise, loud enough to be compressed hard throughout.
    const auto input = test::noise (2, 6000);
    Settings settings { .thresholdDb = -40.0,
                        .ratio = 8.0,
                        .attackMs = 0.1,
                        .autoGain = true,
                        .saturation = true,
                        .driveDb = 24.0,
                        .processingMode = mode,
                        .lookaheadMs = lookaheadMs };
    settings.dryDb = 0.0;
    settings.wetDb = off;
    CHECK (test::played (settings, sampleRate, input, cut) == test::delayed (input, latencySamples (mode, sampleRate)));
}

TEST_CASE ("Auto Gain applies to the Wet path only: with Wet Off, the output is the Dry path alone")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const auto input = test::noise (2, 6000);
    // Auto Gain on at a curve it makes up a lot of gain for: Threshold -40 dB at 4:1.
    Settings settings { .thresholdDb = -40.0, .ratio = 4.0, .autoGain = true, .processingMode = mode };
    settings.dryDb = 0.0;
    settings.wetDb = off;
    CHECK (test::played (settings, 48000.0, input) == test::delayed (input, latencySamples (mode, 48000.0)));
}

TEST_CASE ("Parallel compression: on a steady tone, the output is the Dry path plus the compressed Wet path at their levels")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto [dryDb, wetDb, dryGain, wetGain] = GENERATE (table<double, double, double, double> ({
        { 0.0, 0.0, 1.0, 1.0 },
        { -10.0, 3.0, 0.316227766016838, 1.41253754462275 },
        { 6.0, -20.0, 1.99526231496888, 0.1 },
    }));
    CAPTURE (static_cast<int> (mode), sampleRate, dryDb, wetDb);
    // A steady level 12 dB over the default Threshold, so the compressor's defaults compress it.
    const double levelDb = -6.0;
    const auto input = test::steadyLevel (1, static_cast<int> (0.5 * sampleRate), levelDb);
    const auto level = [&] (const Settings& settings) {
        return std::abs (static_cast<double> (test::played (settings, sampleRate, input).front().back()));
    };
    // The compressed Wet path alone, at 0 dB.
    const double compressed = level ({ .processingMode = mode });
    REQUIRE (compressed < 0.5 * std::pow (10.0, levelDb / 20.0)); // compressed by more than 6 dB
    Settings parallel { .processingMode = mode };
    parallel.dryDb = dryDb;
    parallel.wetDb = wetDb;
    const double expected = dryGain * std::pow (10.0, levelDb / 20.0) + wetGain * compressed;
    CHECK (std::abs (20.0 * std::log10 (level (parallel) / expected)) < 0.01);
}

TEST_CASE ("A non-finite input sample reaches the output through neither path")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const auto input = test::noise (2, 8192);
    auto broken = input, silenced = input;
    for (const std::size_t i : { 100u, 2000u, 3001u, 5000u })
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            broken[ch][i] = i % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][i] = 0.0f;
        }
    Settings settings { .autoGain = true, .processingMode = mode, .lookaheadMs = 3.0 };
    settings.dryDb = 0.0;
    CHECK (test::played (settings, 48000.0, broken) == test::played (settings, 48000.0, silenced));
}

TEST_CASE ("The Engine does not allocate while Dry and Wet move, in either Processing Mode")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (48000.0, blockSize, 2);
    auto audio = test::noise (2, blockSize);
    std::vector<float*> main { audio[0].data(), audio[1].data() };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        Settings settings { .processingMode = (block / 5) % 2 == 0 ? zeroLatency : highQuality };
        settings.dryDb = block % 3 == 0 ? off : -6.0 * (block % 4);
        settings.wetDb = block % 7 == 0 ? off : 12.0 - (block % 5);
        engine.setSettings (settings);
        engine.process ({ main.data(), 2, block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize });
    }
    REQUIRE (guard.allocations() == 0);
}
