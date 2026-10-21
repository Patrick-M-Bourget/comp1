#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

// A steady input level in dB, and the output level dyn-lab gives for it.
struct Row
{
    double inputDb;
    double outputDb;
};

struct StaticCurve
{
    std::string command; // the dyn-lab command that printed rows
    double driveDb;
    std::vector<Row> rows;
};

// Saturation on at driveDb, with the compressor passing everything (Ratio 1:1), so the output is the stage's alone.
Settings saturatedAt (double driveDb)
{
    return { .ratio = 1.0, .saturation = true, .driveDb = driveDb };
}

// Each table is the static curve the dyn-lab command named with it printed, copied as printed (to 0.01 dB).
const std::vector<StaticCurve>& staticCurves()
{
    static const std::vector<StaticCurve> all {
        { "python3 tools/dyn-lab/dynlab.py saturation",
          6.0,
          { { -36.00, -36.00 }, { -30.00, -30.00 }, { -24.00, -24.00 }, { -18.00, -18.00 }, { -12.00, -12.00 },
            { -9.00, -9.00 }, { -6.00, -6.00 }, { -3.00, -3.14 }, { 0.00, -1.09 }, { 3.00, -0.21 }, { 6.00, -0.00 } } },
        { "python3 tools/dyn-lab/dynlab.py saturation --drive 12",
          12.0,
          { { -36.00, -36.00 }, { -24.00, -24.00 }, { -15.00, -15.00 }, { -12.00, -12.00 }, { -9.00, -9.14 },
            { -6.00, -7.09 }, { -3.00, -6.21 }, { 0.00, -6.00 }, { 3.00, -5.98 }, { 6.00, -5.98 } } },
        { "python3 tools/dyn-lab/dynlab.py saturation --drive 24",
          24.0,
          { { -36.00, -36.00 }, { -27.00, -27.00 }, { -24.00, -24.00 }, { -21.00, -21.14 }, { -18.00, -19.09 },
            { -15.00, -18.21 }, { -12.00, -18.00 }, { -9.00, -17.98 }, { -6.00, -17.98 }, { 0.00, -17.98 },
            { 6.00, -17.98 } } },
    };
    return all;
}

} // namespace

TEST_CASE ("At a steady level the Saturation stage follows dyn-lab's static curve within 0.01 dB")
{
    const auto& curve = GENERATE (from_range (staticCurves()));
    const int numChannels = GENERATE (1, 2);
    CAPTURE (curve.command, numChannels);
    for (const auto& row : curve.rows)
    {
        CAPTURE (row.inputDb);
        CHECK (std::abs (test::steadyOutputDb (saturatedAt (curve.driveDb), row.inputDb, 48000.0, numChannels, 0.1, test::constantLevel)
                         - row.outputDb)
               <= 0.01);
    }
}

TEST_CASE ("Before the compressor, Saturation feeds its detection; after it, the compressor hears the input")
{
    // python3 tools/dyn-lab/dynlab.py saturation --drive 12 --level 12: at the compressor's defaults, a steady +12 dB
    // input. After's figure is the static curve of Saturation off's output: the same Gain Reduction, then the shape.
    // Before's Gain Reduction is the saturated signal's, less.
    const double sampleRate = GENERATE (44100.0, 96000.0);
    CAPTURE (sampleRate);
    const auto steadyDb = [sampleRate] (const Settings& settings) {
        return test::steadyOutputDb (settings, 12.0, sampleRate, 2, 0.5, test::constantLevel);
    };
    CHECK (std::abs (steadyDb ({ .driveDb = 12.0 }) - -10.50) <= 0.01);
    CHECK (std::abs (steadyDb ({ .saturation = true, .driveDb = 12.0, .saturationPosition = SaturationPosition::Before }) - -14.99) <= 0.01);
    CHECK (std::abs (steadyDb ({ .saturation = true, .driveDb = 12.0, .saturationPosition = SaturationPosition::After }) - -10.52) <= 0.01);
}

namespace
{

// Noise with peaks up to +6 dBFS, far into the shape at every Drive above 0 dB.
test::Channels loudNoise (int numSamples)
{
    auto audio = test::noise (2, numSamples, 3);
    for (auto& channel : audio)
        for (auto& sample : channel)
            sample *= 4.0f;
    return audio;
}

} // namespace

TEST_CASE ("With Saturation off, the output is the same at any Drive or Saturation Position, bit for bit")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    const auto input = loudNoise (6000);
    const auto reference = test::played ({}, sampleRate, input, cut);
    for (const double driveDb : { 0.0, 24.0 })
        for (const auto position : { SaturationPosition::Before, SaturationPosition::After })
        {
            CAPTURE (driveDb, static_cast<int> (position));
            CHECK (test::played ({ .saturation = false, .driveDb = driveDb, .saturationPosition = position }, sampleRate, input, cut) == reference);
        }
}

TEST_CASE ("At Drive 0 dB, Saturation passes anything at or below full scale through bit for bit, with no delay")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE (from_range (test::blockCuts()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    // High Quality runs at the base rate until Oversampling (#10): there too the stage adds nothing to its latency.
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (sampleRate, cut, static_cast<int> (position), static_cast<int> (mode));
    // Noise up to full scale, with full scale itself, of both signs, among it.
    auto input = test::noise (2, 6000, 5);
    for (auto& channel : input)
    {
        for (auto& sample : channel)
            sample *= 2.0f;
        channel[100] = 1.0f;
        channel[101] = -1.0f;
        channel[2000] = -1.0f;
    }
    const auto off = test::played ({ .processingMode = mode }, sampleRate, input, cut);
    CHECK (test::played ({ .saturation = true, .driveDb = 0.0, .saturationPosition = position, .processingMode = mode }, sampleRate, input, cut) == off);
}

TEST_CASE ("Saturation never makes a non-finite output, from a non-finite input or a near-equal pair of samples")
{
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (static_cast<int> (position));
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    auto input = loudNoise (2000);
    for (auto& channel : input)
    {
        channel[10] = nan;
        channel[11] = inf;
        channel[12] = -inf;
        // Samples over the knee whose driven values are a hair apart, under ADAA's equal-within, and some exactly
        // equal.
        for (std::size_t i = 500; i < 1500; ++i)
            channel[i] = i % 2 == 0 ? 0.75f : std::nextafter (0.75f, 1.0f);
        channel[1500] = channel[1501] = -3.0f;
    }
    const auto output = test::played ({ .ratio = 1.0, .saturation = true, .driveDb = 24.0, .saturationPosition = position }, 48000.0, input, { 64 });
    for (const auto& channel : output)
        for (const float sample : channel)
            REQUIRE (std::isfinite (sample));
    // Through the near-equal pairs, the residual is the shape's own: 0.75 (-2.5 dB) at Drive 24 dB comes out at
    // -17.98 dB (python3 tools/dyn-lab/dynlab.py saturation --drive 24 puts -3 dB and above there).
    CHECK (std::abs (20.0 * std::log10 (output.front()[1000]) - -17.98) <= 0.01);
}
