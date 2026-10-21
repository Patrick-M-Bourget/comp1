#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace comp1;

namespace
{

constexpr double sampleRate = 48000.0;

// A square wave peaking at -9 dB at the defaults: Clean reads its peak, 9 dB over Threshold, and takes 6.75 dB off; VCA
// reads its RMS level 3.01 dB higher and takes 9.01 dB off (python3 tools/dyn-lab/dynlab.py transfer, at 9 and
// 12.01 dB over Threshold, 3:4 of it above the Knee).
constexpr double levelDb = -9.0;
constexpr double cleanReductionDb = 6.75;
const double vcaReductionDb = 0.75 * (9.0 + test::sineCalibrationDb);

// The Gain Reduction on each sample of the square wave, with from's Character for a second, then to's.
std::vector<double> acrossChange (Character from, Character to)
{
    const int length = static_cast<int> (2.0 * sampleRate);
    const auto input = test::steadyLevel (1, length, levelDb);
    // Colour 0%, so the output's gain is the compressor's alone.
    const auto output = test::played ({ .character = from, .colour = 0.0 },
                                      sampleRate,
                                      input,
                                      { 512 },
                                      nullptr,
                                      test::SettingsChange { .at = length / 2, .settings = { .character = to, .colour = 0.0 } });
    return test::gainReductionDb (input.front(), output.front());
}

} // namespace

TEST_CASE ("A Character change lands, and the incoming Character starts from the outgoing one's Gain Reduction")
{
    const bool toVca = GENERATE (true, false);
    CAPTURE (toVca);
    const auto reduction = acrossChange (toVca ? Character::Clean : Character::VCA, toVca ? Character::VCA : Character::Clean);
    const auto half = reduction.size() / 2;
    const double before = toVca ? cleanReductionDb : vcaReductionDb, after = toVca ? vcaReductionDb : cleanReductionDb;
    CHECK (std::abs (reduction[half - 1] - before) <= 0.01);
    CHECK (std::abs (reduction.back() - after) <= 0.01);
    // From the change on, the Gain Reduction moves from one Character's to the other's and never strays outside them, as
    // it would if the incoming detector started from none and attacked from there.
    const auto [lowest, highest] = std::minmax_element (reduction.begin() + static_cast<std::ptrdiff_t> (half), reduction.end());
    CHECK (*lowest >= std::min (before, after) - 0.01);
    CHECK (*highest <= std::max (before, after) + 0.01);
}
