#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

// A step response's times in ms, as dyn-lab's step prints them: Release counts from when it begins, after Hold.
struct Step
{
    double attack63, attack90, release63, release90, hold;
};

struct Rate
{
    double sampleRate;
    Step step;
};

struct Response
{
    std::string command; // the dyn-lab command that printed rates
    Settings settings;
    double gainReductionDb; // the Gain Reduction 20 dB over Threshold: (1 - 1/Ratio) 20 dB, capped at Range
    std::vector<Rate> rates;
};

// Each table is the output of the dyn-lab command named with it, copied as printed.
const std::vector<Response>& responses()
{
    static const std::vector<Response> all {
        { "python3 tools/dyn-lab/dynlab.py step --rates 44100 48000 88200 96000 176400 192000",
          {},
          15.0,
          { { 44100, { 10.0000, 23.0385, 100.0227, 230.2721, 0.0000 } },
            { 48000, { 10.0208, 23.0417, 100.0208, 230.2708, 0.0000 } },
            { 88200, { 10.0000, 23.0272, 100.0113, 230.2608, 0.0000 } },
            { 96000, { 10.0104, 23.0312, 100.0104, 230.2604, 0.0000 } },
            { 176400, { 10.0000, 23.0272, 100.0057, 230.2608, 0.0000 } },
            { 192000, { 10.0052, 23.0260, 100.0000, 230.2604, 0.0000 } } } },
        { "python3 tools/dyn-lab/dynlab.py step --attack 0.5 --release 50 --hold 20 --rates 44100 48000 88200 96000 176400 192000",
          { .attackMs = 0.5, .releaseMs = 50.0, .holdMs = 20.0 },
          15.0,
          { { 44100, { 0.5215, 1.1565, 50.0227, 115.1474, 20.0000 } },
            { 48000, { 0.5000, 1.1667, 50.0000, 115.1458, 20.0000 } },
            { 88200, { 0.5102, 1.1565, 50.0113, 115.1361, 20.0000 } },
            { 96000, { 0.5000, 1.1562, 50.0104, 115.1354, 20.0000 } },
            { 176400, { 0.5045, 1.1565, 50.0057, 115.1304, 20.0000 } },
            { 192000, { 0.5000, 1.1562, 50.0052, 115.1302, 20.0000 } } } },
        { "python3 tools/dyn-lab/dynlab.py step --range 6 --hold 20 --rates 48000",
          { .holdMs = 20.0, .rangeDb = 6.0 },
          6.0,
          { { 48000, { 10.0208, 23.0417, 100.0208, 230.2708, 20.0000 } } } },
        // VCA, on the lab's steady test signal for it (test::steadyLevelOffsetDb). Colour is 0%, so the output's gain is the
        // compressor's alone.
        { "python3 tools/dyn-lab/dynlab.py step --character vca",
          { .character = Character::VCA, .colour = 0.0 },
          15.0,
          { { 44100, { 12.1088, 25.5102, 111.7914, 242.0635, 0.0454 } },
            { 48000, { 12.1042, 25.5208, 111.7917, 242.0625, 0.0417 } },
            { 96000, { 12.1042, 25.5208, 111.7812, 242.0417, 0.0625 } } } },
        { "python3 tools/dyn-lab/dynlab.py step --character vca --attack 0.5 --release 50 --hold 20",
          { .character = Character::VCA, .attackMs = 0.5, .releaseMs = 50.0, .holdMs = 20.0, .colour = 0.0 },
          15.0,
          { { 44100, { 1.7007, 5.5329, 50.3175, 115.4422, 20.0000 } },
            { 48000, { 1.6875, 5.5417, 50.3125, 115.4375, 20.0000 } },
            { 96000, { 1.6875, 5.5312, 50.3021, 115.4375, 20.0000 } } } },
    };
    return all;
}

constexpr double stepDb = 20.0;

// The Gain Reduction in dB on each sample of a steady level held for seconds, then of another, through one Engine:
// the first level settles it, the second is the step.
std::vector<double> gainReductionAfterStep (const Settings& settings, double sampleRate, double fromDb, double toDb, double seconds)
{
    const int length = static_cast<int> (seconds * sampleRate);
    auto before = test::steadyLevel (1, length, fromDb);
    auto after = test::steadyLevel (1, length, toDb);
    const auto input = after.front();
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings (settings);
    test::processInBlocks (engine, before, { 512 });
    test::processInBlocks (engine, after, { 512 });
    return test::gainReductionDb (input, after.front());
}

} // namespace

TEST_CASE ("Attack, Release and Hold match dyn-lab's step response within one sample, at every sample rate", "[sweep]")
{
    const auto& response = GENERATE (from_range (responses()));
    const auto& rate = GENERATE_REF (from_range (response.rates));
    CAPTURE (response.command, rate.sampleRate);
    const double sampleMs = 1000.0 / rate.sampleRate;
    const auto& settings = response.settings;
    const double offset = test::steadyLevelOffsetDb (settings.character);
    const double quiet = settings.thresholdDb - stepDb + offset, loud = settings.thresholdDb + stepDb + offset;

    // Up from no Gain Reduction (the quiet level is below the Knee) to the full amount, for Attack.
    const auto attack = test::crossings (gainReductionAfterStep (settings, rate.sampleRate, quiet, loud, 1.0), 0.0, response.gainReductionDb);
    REQUIRE (attack.ninety);
    CHECK (std::abs (*attack.sixtyThree * sampleMs - rate.step.attack63) <= sampleMs);
    CHECK (std::abs (*attack.ninety * sampleMs - rate.step.attack90) <= sampleMs);

    // And back down, for Hold and Release.
    const auto release = test::crossings (gainReductionAfterStep (settings, rate.sampleRate, loud, quiet, 2.0), response.gainReductionDb, 0.0);
    REQUIRE (release.ninety);
    CHECK (std::abs (release.held * sampleMs - rate.step.hold) <= sampleMs);
    CHECK (std::abs ((*release.sixtyThree - release.held) * sampleMs - rate.step.release63) <= sampleMs);
    CHECK (std::abs ((*release.ninety - release.held) * sampleMs - rate.step.release90) <= sampleMs);
}
