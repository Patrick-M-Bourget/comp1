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

struct Curve
{
    std::string command; // the dyn-lab command that printed rows
    Settings settings;
    std::vector<Row> rows;
};

constexpr double infinity = std::numeric_limits<double>::infinity();

// Each table is the output of the dyn-lab command named with it, copied as printed (to 0.01 dB).
const std::vector<Curve>& curves()
{
    static const std::vector<Curve> all {
        { "python3 tools/dyn-lab/dynlab.py transfer",
          {},
          { { -33.00, -33.00 }, { -30.00, -30.00 }, { -27.00, -27.00 }, { -24.00, -24.00 }, { -21.00, -21.00 },
            { -18.00, -18.56 }, { -15.00, -17.25 }, { -12.00, -16.50 }, { -9.00, -15.75 }, { -6.00, -15.00 },
            { -3.00, -14.25 }, { 0.00, -13.50 }, { 3.00, -12.75 }, { 6.00, -12.00 }, { 9.00, -11.25 } } },
        { "python3 tools/dyn-lab/dynlab.py transfer --ratio inf",
          { .ratio = infinity },
          { { -33.00, -33.00 }, { -30.00, -30.00 }, { -27.00, -27.00 }, { -24.00, -24.00 }, { -21.00, -21.00 },
            { -18.00, -18.75 }, { -15.00, -18.00 }, { -12.00, -18.00 }, { -9.00, -18.00 }, { -6.00, -18.00 },
            { -3.00, -18.00 }, { 0.00, -18.00 }, { 3.00, -18.00 }, { 6.00, -18.00 }, { 9.00, -18.00 } } },
        { "python3 tools/dyn-lab/dynlab.py transfer --knee 0",
          { .kneeDb = 0.0 },
          { { -30.00, -30.00 }, { -27.00, -27.00 }, { -24.00, -24.00 }, { -21.00, -21.00 }, { -18.00, -18.00 },
            { -15.00, -17.25 }, { -12.00, -16.50 }, { -9.00, -15.75 }, { -6.00, -15.00 }, { -3.00, -14.25 },
            { 0.00, -13.50 }, { 3.00, -12.75 }, { 6.00, -12.00 } } },
        { "python3 tools/dyn-lab/dynlab.py transfer --knee 72",
          { .kneeDb = 72.0 },
          { { -66.00, -66.00 }, { -63.00, -63.00 }, { -60.00, -60.00 }, { -57.00, -57.00 }, { -54.00, -54.00 },
            { -51.00, -51.05 }, { -48.00, -48.19 }, { -45.00, -45.42 }, { -42.00, -42.75 }, { -39.00, -40.17 },
            { -36.00, -37.69 }, { -33.00, -35.30 }, { -30.00, -33.00 }, { -27.00, -30.80 }, { -24.00, -28.69 },
            { -21.00, -26.67 }, { -18.00, -24.75 }, { -15.00, -22.92 }, { -12.00, -21.19 }, { -9.00, -19.55 },
            { -6.00, -18.00 }, { -3.00, -16.55 }, { 0.00, -15.19 }, { 3.00, -13.92 }, { 6.00, -12.75 },
            { 9.00, -11.67 }, { 12.00, -10.69 }, { 15.00, -9.80 }, { 18.00, -9.00 }, { 21.00, -8.25 },
            { 24.00, -7.50 }, { 27.00, -6.75 }, { 30.00, -6.00 }, { 33.00, -5.25 }, { 36.00, -4.50 },
            { 39.00, -3.75 }, { 42.00, -3.00 } } },
        { "python3 tools/dyn-lab/dynlab.py transfer --range 6",
          { .rangeDb = 6.0 },
          { { -33.00, -33.00 }, { -30.00, -30.00 }, { -27.00, -27.00 }, { -24.00, -24.00 }, { -21.00, -21.00 },
            { -18.00, -18.56 }, { -15.00, -17.25 }, { -12.00, -16.50 }, { -10.00, -16.00 }, { -9.00, -15.00 },
            { -6.00, -12.00 }, { -3.00, -9.00 }, { 0.00, -6.00 }, { 3.00, -3.00 }, { 6.00, 0.00 }, { 9.00, 3.00 } } },
    };
    return all;
}

} // namespace

TEST_CASE ("At a steady level the output follows dyn-lab's Transfer Curve within 0.01 dB", "[sweep]")
{
    const auto& curve = GENERATE (from_range (curves()));
    const int numChannels = GENERATE (1, 2);
    CAPTURE (curve.command, numChannels);
    for (const auto& row : curve.rows)
    {
        CAPTURE (row.inputDb);
        // Half a second at the level, for the Gain Reduction to settle (Attack is 10 ms in every table).
        CHECK (std::abs (test::steadyOutputDb (curve.settings, row.inputDb, 48000.0, numChannels) - row.outputDb) <= 0.01);
    }
}

TEST_CASE ("On VCA, at a steady level the output follows dyn-lab's Transfer Curve within 0.01 dB", "[sweep]")
{
    // `python3 tools/dyn-lab/dynlab.py transfer --character vca`, with each table's other options, prints the tables
    // above as they are: VCA's Transfer Curve is Clean's, on its RMS Detection Level. The lab's steady test signal for
    // VCA is a square wave sineCalibrationDb under each level (tools/dyn-lab/USAGE.md, "VCA"), so the output level is
    // read sineCalibrationDb over the square wave's. Clean, reading the square wave's peak, would read each level
    // sineCalibrationDb low. Mono: each channel's detection is Clean's, which the test above runs on stereo. 0.2 s at
    // each level is 20 of Attack's time constants, and the RMS level settles in 5 ms.
    const auto& curve = GENERATE (from_range (curves()));
    CAPTURE (curve.command);
    auto settings = curve.settings;
    settings.character = Character::VCA;
    for (const auto& row : curve.rows)
    {
        CAPTURE (row.inputDb);
        const double outputDb = test::steadyOutputDb (settings, row.inputDb - test::sineCalibrationDb, 48000.0, 1, 0.2) + test::sineCalibrationDb;
        CHECK (std::abs (outputDb - row.outputDb) <= 0.01);
    }
}
