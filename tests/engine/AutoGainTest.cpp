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

// The settings tools/dyn-lab/USAGE.md lists for the Auto Gain check, each on every Character, and FET's Style defaults,
// each with the estimate its command prints, with Auto Gain on: the Transfer Curve read the Character's Timing Offset
// higher, for the row's Attack, Release, Auto Release and Hold.
const std::vector<Estimate>& estimates()
{
    static const std::vector<Estimate> all {
        { "python3 tools/dyn-lab/dynlab.py autogain --character clean", { .character = Character::Clean, .autoGain = true }, 2.8308 },
        { "python3 tools/dyn-lab/dynlab.py autogain --character vca", { .character = Character::VCA, .autoGain = true }, 3.5681 },
        { "python3 tools/dyn-lab/dynlab.py autogain --character fet", { .character = Character::FET, .autoGain = true }, 3.3906 },
        { "python3 tools/dyn-lab/dynlab.py autogain --character opto", { .character = Character::Opto, .autoGain = true }, 3.3922 },
        { "python3 tools/dyn-lab/dynlab.py autogain --ratio inf --character clean", { .character = Character::Clean, .ratio = infinity, .autoGain = true }, 3.7745 },
        { "python3 tools/dyn-lab/dynlab.py autogain --ratio inf --character vca", { .character = Character::VCA, .ratio = infinity, .autoGain = true }, 4.7575 },
        { "python3 tools/dyn-lab/dynlab.py autogain --ratio inf --character fet", { .character = Character::FET, .ratio = infinity, .autoGain = true }, 4.5208 },
        { "python3 tools/dyn-lab/dynlab.py autogain --ratio inf --character opto", { .character = Character::Opto, .ratio = infinity, .autoGain = true }, 4.5230 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 0 --character clean", { .character = Character::Clean, .kneeDb = 0.0, .autoGain = true }, 2.8064 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 0 --character vca", { .character = Character::VCA, .kneeDb = 0.0, .autoGain = true }, 3.5443 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 0 --character fet", { .character = Character::FET, .kneeDb = 0.0, .autoGain = true }, 3.3638 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 0 --character opto", { .character = Character::Opto, .kneeDb = 0.0, .autoGain = true }, 3.3655 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 72 --character clean", { .character = Character::Clean, .kneeDb = 72.0, .autoGain = true }, 6.3437 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 72 --character vca", { .character = Character::VCA, .kneeDb = 72.0, .autoGain = true }, 7.1164 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 72 --character fet", { .character = Character::FET, .kneeDb = 72.0, .autoGain = true }, 6.9339 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 72 --character opto", { .character = Character::Opto, .kneeDb = 72.0, .autoGain = true }, 6.9356 },
        { "python3 tools/dyn-lab/dynlab.py autogain --range 6 --character clean", { .character = Character::Clean, .rangeDb = 6.0, .autoGain = true }, 1.9860 },
        { "python3 tools/dyn-lab/dynlab.py autogain --range 6 --character vca", { .character = Character::VCA, .rangeDb = 6.0, .autoGain = true }, 2.2987 },
        { "python3 tools/dyn-lab/dynlab.py autogain --range 6 --character fet", { .character = Character::FET, .rangeDb = 6.0, .autoGain = true }, 2.2295 },
        { "python3 tools/dyn-lab/dynlab.py autogain --range 6 --character opto", { .character = Character::Opto, .rangeDb = 6.0, .autoGain = true }, 2.2301 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --character clean", { .character = Character::Clean, .thresholdDb = -30.0, .autoGain = true }, 7.9161 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --character vca", { .character = Character::VCA, .thresholdDb = -30.0, .autoGain = true }, 9.1233 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --character fet", { .character = Character::FET, .thresholdDb = -30.0, .autoGain = true }, 8.8375 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --character opto", { .character = Character::Opto, .thresholdDb = -30.0, .autoGain = true }, 8.8401 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf --character clean", { .character = Character::Clean, .thresholdDb = -30.0, .ratio = infinity, .autoGain = true }, 10.5548 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf --character vca", { .character = Character::VCA, .thresholdDb = -30.0, .ratio = infinity, .autoGain = true }, 12.1644 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf --character fet", { .character = Character::FET, .thresholdDb = -30.0, .ratio = infinity, .autoGain = true }, 11.7833 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf --character opto", { .character = Character::Opto, .thresholdDb = -30.0, .ratio = infinity, .autoGain = true }, 11.7868 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2 --character clean", { .character = Character::Clean, .thresholdDb = -6.0, .ratio = 2.0, .autoGain = true }, 0.2114 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2 --character vca", { .character = Character::VCA, .thresholdDb = -6.0, .ratio = 2.0, .autoGain = true }, 0.3896 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2 --character fet", { .character = Character::FET, .thresholdDb = -6.0, .ratio = 2.0, .autoGain = true }, 0.3435 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2 --character opto", { .character = Character::Opto, .thresholdDb = -6.0, .ratio = 2.0, .autoGain = true }, 0.3439 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --character clean", { .character = Character::Clean, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoGain = true }, 17.1440 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --character vca", { .character = Character::VCA, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoGain = true }, 15.6245 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --character fet", { .character = Character::FET, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoGain = true }, 17.5701 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --character opto", { .character = Character::Opto, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoGain = true }, 17.8126 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --auto-release --character clean", { .character = Character::Clean, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoRelease = true, .autoGain = true }, 17.5182 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --auto-release --character vca", { .character = Character::VCA, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoRelease = true, .autoGain = true }, 16.1964 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --auto-release --character fet", { .character = Character::FET, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoRelease = true, .autoGain = true }, 17.9458 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 0.5 --release 400 --auto-release --character opto", { .character = Character::Opto, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 400.0, .autoRelease = true, .autoGain = true }, 18.5391 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 30 --release 20 --character clean", { .character = Character::Clean, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 30.0, .releaseMs = 20.0, .autoGain = true }, 8.3310 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 30 --release 20 --character vca", { .character = Character::VCA, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 30.0, .releaseMs = 20.0, .autoGain = true }, 13.1054 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 30 --release 20 --character fet", { .character = Character::FET, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 30.0, .releaseMs = 20.0, .autoGain = true }, 9.9980 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 30 --release 20 --character opto", { .character = Character::Opto, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 30.0, .releaseMs = 20.0, .autoGain = true }, 11.3861 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 5 --auto-release --character clean", { .character = Character::Clean, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 5.0, .autoRelease = true, .autoGain = true }, 15.2646 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 5 --auto-release --character vca", { .character = Character::VCA, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 5.0, .autoRelease = true, .autoGain = true }, 15.1144 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 5 --auto-release --character fet", { .character = Character::FET, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 5.0, .autoRelease = true, .autoGain = true }, 16.1635 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -36 --ratio 6 --knee 3 --attack 5 --auto-release --character opto", { .character = Character::Opto, .thresholdDb = -36.0, .ratio = 6.0, .kneeDb = 3.0, .attackMs = 5.0, .autoRelease = true, .autoGain = true }, 15.6509 },
        { "python3 tools/dyn-lab/dynlab.py autogain --hold 20 --character clean", { .character = Character::Clean, .holdMs = 20.0, .autoGain = true }, 4.4567 },
        { "python3 tools/dyn-lab/dynlab.py autogain --hold 20 --character vca", { .character = Character::VCA, .holdMs = 20.0, .autoGain = true }, 3.6594 },
        { "python3 tools/dyn-lab/dynlab.py autogain --hold 20 --character fet", { .character = Character::FET, .holdMs = 20.0, .autoGain = true }, 4.7757 },
        { "python3 tools/dyn-lab/dynlab.py autogain --hold 20 --character opto", { .character = Character::Opto, .holdMs = 20.0, .autoGain = true }, 4.6562 },
        { "python3 tools/dyn-lab/dynlab.py autogain --style fet", { .character = Character::FET, .kneeDb = 3.0, .attackMs = 0.5, .releaseMs = 50.0, .autoGain = true }, 4.4837 },
    };
    return all;
}

// dyn-lab's Auto Gain grid (tools/dyn-lab/USAGE.md, "autogaingrid"): its depths as Threshold, Ratio and Knee, its
// Attacks and Releases in ms, each with Auto Release off and on at Hold 0 ms; then its Holds in ms at Attack 10 ms and
// Release 100 ms, Auto Release off and on, at every depth. Colour 0%, Range 60 dB, Zero Latency and the Internal
// Detection Source, the defaults but Colour.
struct Depth
{
    double thresholdDb, ratio, kneeDb;
};
const Depth gridDepths[] { { -6.0, 2.0, 6.0 },       { -18.0, 4.0, 6.0 }, { -18.0, 4.0, 3.0 }, { -30.0, infinity, 6.0 },
                           { -36.0, 8.0, 6.0 },      { -28.0, 20.0, 3.0 }, { -42.0, 6.0, 12.0 } };
const double gridAttacksMs[] { 0.5, 2.0, 5.0, 10.0, 30.0 };
const double gridReleasesMs[] { 20.0, 50.0, 100.0, 400.0 };
const double gridHoldsMs[] { 20.0, 100.0 };

std::vector<Settings> autoGainGrid (Character character)
{
    std::vector<Settings> cells;
    const auto cell = [character] (const Depth& depth, double attackMs, double releaseMs, bool autoRelease, double holdMs) {
        return Settings { .character = character,
                          .thresholdDb = depth.thresholdDb,
                          .ratio = depth.ratio,
                          .kneeDb = depth.kneeDb,
                          .attackMs = attackMs,
                          .releaseMs = releaseMs,
                          .autoRelease = autoRelease,
                          .holdMs = holdMs,
                          .colour = 0.0,
                          .autoGain = true };
    };
    for (const auto& depth : gridDepths)
    {
        for (const double attackMs : gridAttacksMs)
            for (const double releaseMs : gridReleasesMs)
                for (const bool autoRelease : { false, true })
                    cells.push_back (cell (depth, attackMs, releaseMs, autoRelease, 0.0));
        for (const double holdMs : gridHoldsMs)
            for (const bool autoRelease : { false, true })
                cells.push_back (cell (depth, 10.0, 100.0, autoRelease, holdMs));
    }
    return cells;
}

} // namespace

TEST_CASE ("Auto Gain adds dyn-lab's estimate within 0.01 dB, worked out from the settings, on the Character each row names")
{
    const auto& estimate = GENERATE (from_range (estimates()));
    CAPTURE (estimate.command);
    // At -120 dB nothing is compressed (the lowest Knee here starts at -54 dB), so the output level is Auto Gain's.
    const double gainDb = test::steadyOutputDb (estimate.settings, -120.0, 48000.0) + 120.0;
    CAPTURE (gainDb);
    CHECK (std::abs (gainDb - estimate.autoGainDb) <= 0.01);
}

TEST_CASE ("Auto Gain's estimate reads neither Lookahead, the Detection EQ, Stereo Link, Stereo Placement, the Detection Source, Colour nor Saturation")
{
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    CAPTURE (static_cast<int> (character));
    const Settings settings { .character = character, .attackMs = 0.5, .releaseMs = 400.0, .autoGain = true };
    const double gainDb = test::steadyOutputDb (settings, -120.0, 48000.0, 2) + 120.0;
    auto routed = settings;
    routed.processingMode = ProcessingMode::HighQuality;
    routed.lookaheadMs = 5.0;
    routed.detectionEq.highPass.on = true;
    routed.stereoLink = 0.0;
    routed.stereoPlacement = StereoPlacement::Mid;
    routed.detectionSource = DetectionSource::External;
    routed.colour = 0.0;
    routed.saturation = true;
    routed.driveDb = 0.0;
    CHECK (std::abs (test::steadyOutputDb (routed, -120.0, 48000.0, 2, 1.0) + 120.0 - gainDb) <= 0.01);
}

TEST_CASE ("At Comp 0%, Ratio 1:1, Auto Gain adds 0 dB on every Character and at every timing")
{
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    const auto& [attackMs, releaseMs, autoRelease, holdMs] = GENERATE (table<double, double, bool, double> (
        { { 10.0, 100.0, false, 0.0 }, { 0.5, 400.0, true, 0.0 }, { 30.0, 20.0, false, 100.0 } }));
    CAPTURE (static_cast<int> (character), attackMs, releaseMs, autoRelease, holdMs);
    const Settings settings { .character = character,
                              .comp = 0.0,
                              .compFollow = true,
                              .attackMs = attackMs,
                              .releaseMs = releaseMs,
                              .autoRelease = autoRelease,
                              .holdMs = holdMs };
    CHECK (std::abs (test::steadyOutputDb (settings, -120.0, 48000.0) + 120.0) <= 1.0e-6);
}

TEST_CASE ("With Auto Gain on, the reference signal keeps its level within dyn-lab's tolerance at each row", "[sweep]")
{
    const auto& estimate = GENERATE (from_range (estimates()));
    CAPTURE (estimate.command);
    // The lab's check runs without Colour, which Auto Gain doesn't compensate.
    auto settings = estimate.settings;
    settings.colour = 0.0;
    const double differenceDb = test::referenceLevelDifferenceDb (settings);
    CAPTURE (differenceDb);
    CHECK (std::abs (differenceDb) <= test::autoGainToleranceDb);
}

TEST_CASE ("With Auto Gain on, the reference signal keeps its level within dyn-lab's tolerance over its Auto Gain grid, on every Character", "[sweep]")
{
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    const auto cells = autoGainGrid (character);
    REQUIRE (cells.size() == 308);
    double worstDb = 0.0;
    for (const auto& cell : cells)
    {
        CAPTURE (static_cast<int> (character), cell.thresholdDb, cell.ratio, cell.kneeDb, cell.attackMs, cell.releaseMs, cell.autoRelease, cell.holdMs);
        const double differenceDb = test::referenceLevelDifferenceDb (cell);
        CAPTURE (differenceDb);
        CHECK (std::abs (differenceDb) <= test::autoGainToleranceDb);
        worstDb = std::max (worstDb, std::abs (differenceDb));
    }
    // dyn-lab's worst cell per Character is 0.34 (Clean), 0.07 (VCA), 0.46 (FET) and 0.86 dB (Opto).
    CAPTURE (worstDb);
    CHECK (worstDb <= test::autoGainToleranceDb);
}
