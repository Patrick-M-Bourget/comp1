#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

// A step response's times in ms, as dyn-lab's step --lookahead prints them: Attack counts from the step in the delayed
// audio the listener hears, Hold from when the detector meets the step down, and Release from when it begins.
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
    Settings settings;   // in High Quality, at the command's Lookahead
    double gainReductionDb; // the Gain Reduction 20 dB over Threshold: (1 - 1/Ratio) 20 dB
    std::vector<Rate> rates;
};

constexpr auto highQuality = ProcessingMode::HighQuality;

// Each table is the output of the dyn-lab command named with it, copied as printed.
const std::vector<Response>& responses()
{
    static const std::vector<Response> all {
        { "python3 tools/dyn-lab/dynlab.py step --lookahead 5",
          { .processingMode = highQuality, .lookaheadMs = 5.0 },
          15.0,
          { { 44100, { 5.0000, 18.0385, 100.0227, 230.2721, 0.0000 } },
            { 48000, { 5.0208, 18.0417, 100.0208, 230.2708, 0.0000 } },
            { 96000, { 5.0104, 18.0312, 100.0104, 230.2604, 0.0000 } } } },
        { "python3 tools/dyn-lab/dynlab.py step --lookahead 2 --attack 0.5 --release 50 --hold 20",
          { .attackMs = 0.5, .releaseMs = 50.0, .holdMs = 20.0, .processingMode = highQuality, .lookaheadMs = 2.0 },
          15.0,
          { { 44100, { -1.4785, -0.8435, 50.0227, 115.1474, 20.0000 } },
            { 48000, { -1.5000, -0.8333, 50.0000, 115.1458, 20.0000 } },
            { 96000, { -1.5000, -0.8438, 50.0104, 115.1354, 20.0000 } } } },
    };
    return all;
}

constexpr double stepDb = 20.0;

} // namespace

TEST_CASE ("In High Quality, Lookahead's step response matches dyn-lab's within one sample, against the delayed audio", "[sweep]")
{
    const auto& response = GENERATE (from_range (responses()));
    const auto& rate = GENERATE_REF (from_range (response.rates));
    CAPTURE (response.command, rate.sampleRate);
    const double sampleMs = 1000.0 / rate.sampleRate;
    const auto& settings = response.settings;
    const double quiet = settings.thresholdDb - stepDb, loud = settings.thresholdDb + stepDb;
    const int latency = latencySamples (highQuality, rate.sampleRate);

    // Up, for Attack, from the step the listener hears, at index latency: counted from the Lookahead budget before it,
    // so a Gain Reduction that starts before the step is measured too, then moved to count from the step.
    const int budget = lookaheadBudgetSamples (rate.sampleRate);
    const auto attack = test::crossings (test::heardGainReduction (settings, rate.sampleRate, quiet, loud, 1.0, latency),
                                         0.0,
                                         response.gainReductionDb,
                                         static_cast<std::size_t> (latency - budget));
    REQUIRE (attack.ninety);
    CHECK (std::abs ((*attack.sixtyThree - budget) * sampleMs - rate.step.attack63) <= sampleMs);
    CHECK (std::abs ((*attack.ninety - budget) * sampleMs - rate.step.attack90) <= sampleMs);

    // And down, for Hold and Release, from when the detector meets the step: Lookahead before the listener does.
    const int detectorMeetsIt = latency - static_cast<int> (std::lround (settings.lookaheadMs * rate.sampleRate / 1000.0));
    const auto release = test::crossings (test::heardGainReduction (settings, rate.sampleRate, loud, quiet, 2.0, latency),
                                          response.gainReductionDb,
                                          0.0,
                                          static_cast<std::size_t> (detectorMeetsIt));
    REQUIRE (release.ninety);
    CHECK (std::abs (release.held * sampleMs - rate.step.hold) <= sampleMs);
    CHECK (std::abs ((*release.sixtyThree - release.held) * sampleMs - rate.step.release63) <= sampleMs);
    CHECK (std::abs ((*release.ninety - release.held) * sampleMs - rate.step.release90) <= sampleMs);
}
