#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

struct Estimate
{
    std::string command; // the dyn-lab command that printed autoGainDb
    Settings settings;
    double autoGainDb;
};

constexpr double infinity = std::numeric_limits<double>::infinity();

// The settings tools/dyn-lab/USAGE.md lists for the Auto Gain check, each with the estimate its command prints, with
// Auto Gain on. Each command with --character vca or fet prints the same estimate, the Transfer Curve's, and checks it
// on that Character.
const std::vector<Estimate>& estimates()
{
    static const std::vector<Estimate> all {
        { "python3 tools/dyn-lab/dynlab.py autogain", { .autoGain = true }, 3.2470 },
        { "python3 tools/dyn-lab/dynlab.py autogain --ratio inf", { .ratio = infinity, .autoGain = true }, 4.3294 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 0", { .kneeDb = 0.0, .autoGain = true }, 3.2143 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 72", { .kneeDb = 72.0, .autoGain = true }, 6.7847 },
        { "python3 tools/dyn-lab/dynlab.py autogain --range 6", { .rangeDb = 6.0, .autoGain = true }, 2.1756 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30", { .thresholdDb = -30.0, .autoGain = true }, 8.6042 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf",
          { .thresholdDb = -30.0, .ratio = infinity, .autoGain = true },
          11.4722 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2",
          { .thresholdDb = -6.0, .ratio = 2.0, .autoGain = true },
          0.3075 },
    };
    return all;
}

constexpr double autoGainToleranceDb = 1.0; // dyn-lab's AUTO_GAIN_TOLERANCE

} // namespace

TEST_CASE ("Auto Gain adds dyn-lab's estimate within 0.01 dB, worked out from the settings, on every Character")
{
    const auto& estimate = GENERATE (from_range (estimates()));
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    CAPTURE (estimate.command, static_cast<int> (character));
    auto settings = estimate.settings;
    settings.character = character;
    // At -120 dB nothing is compressed (the lowest Knee here starts at -54 dB), so the output level is Auto Gain's.
    const double gainDb = test::steadyOutputDb (settings, -120.0, 48000.0) + 120.0;
    CHECK (std::abs (gainDb - estimate.autoGainDb) <= 0.01);
}

TEST_CASE ("With Auto Gain on, the reference signal keeps its level within dyn-lab's tolerance, on every Character", "[sweep]")
{
    const auto& estimate = GENERATE (from_range (estimates()));
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    CAPTURE (estimate.command, static_cast<int> (character));
    auto settings = estimate.settings;
    settings.character = character;
    // The lab's check runs without Colour, which Auto Gain doesn't compensate.
    settings.colour = 0.0;
    const double differenceDb = test::referenceLevelDifferenceDb (settings);
    CAPTURE (differenceDb);
    CHECK (std::abs (differenceDb) <= autoGainToleranceDb);
}
