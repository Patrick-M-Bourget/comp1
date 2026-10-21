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

// A release's times in ms, counted from when Release begins, after Hold, as dyn-lab prints them.
struct Release
{
    double release63, release90;
};

// One row of dyn-lab's bursts: the Release after a burst, with Auto Release off and on.
struct Burst
{
    double sampleRate, lengthMs;
    Release off, on;
};

struct Bursts
{
    std::string command; // the dyn-lab command that printed rows
    Settings settings;
    std::vector<Burst> rows;
};

// Each table is the output of the dyn-lab command named with it, copied as printed.
const std::vector<Bursts>& allBursts()
{
    static const std::vector<Bursts> all {
        { "python3 tools/dyn-lab/dynlab.py bursts",
          {},
          { { 44100, 1, { 100.0227, 230.2721 }, { 100.4082, 231.1338 } },
            { 44100, 10, { 100.0227, 230.2721 }, { 103.9002, 238.9116 } },
            { 44100, 100, { 100.0227, 230.2721 }, { 136.8027, 311.4059 } },
            { 44100, 1000, { 100.0227, 230.2721 }, { 332.6077, 716.1451 } },
            { 44100, 3000, { 100.0227, 230.2721 }, { 440.1587, 922.2222 } },
            { 48000, 1, { 100.0208, 230.2708 }, { 100.3958, 231.1458 } },
            { 48000, 10, { 100.0208, 230.2708 }, { 103.8958, 238.9167 } },
            { 48000, 100, { 100.0208, 230.2708 }, { 136.8125, 311.3958 } },
            { 48000, 1000, { 100.0208, 230.2708 }, { 332.6042, 716.1458 } },
            { 48000, 3000, { 100.0208, 230.2708 }, { 440.1458, 922.2292 } },
            { 96000, 1, { 100.0104, 230.2604 }, { 100.3958, 231.1354 } },
            { 96000, 10, { 100.0104, 230.2604 }, { 103.8854, 238.9062 } },
            { 96000, 100, { 100.0104, 230.2604 }, { 136.7917, 311.3854 } },
            { 96000, 1000, { 100.0104, 230.2604 }, { 332.5833, 716.1250 } },
            { 96000, 3000, { 100.0104, 230.2604 }, { 440.1250, 922.1979 } } } },
        { "python3 tools/dyn-lab/dynlab.py bursts --attack 0.5 --release 50 --hold 20",
          { .attackMs = 0.5, .releaseMs = 50.0, .holdMs = 20.0 },
          { { 44100, 1, { 50.0227, 115.1474 }, { 50.2041, 115.5782 } },
            { 44100, 10, { 50.0227, 115.1474 }, { 51.9501, 119.5465 } },
            { 44100, 100, { 50.0227, 115.1474 }, { 68.5261, 156.8707 } },
            { 44100, 1000, { 50.0227, 115.1474 }, { 169.9773, 377.2789 } },
            { 44100, 3000, { 50.0227, 115.1474 }, { 227.7098, 497.2336 } },
            { 48000, 1, { 50.0000, 115.1458 }, { 50.2083, 115.5833 } },
            { 48000, 10, { 50.0000, 115.1458 }, { 51.9583, 119.5417 } },
            { 48000, 100, { 50.0000, 115.1458 }, { 68.5417, 156.8750 } },
            { 48000, 1000, { 50.0000, 115.1458 }, { 169.9583, 377.2708 } },
            { 48000, 3000, { 50.0000, 115.1458 }, { 227.7083, 497.2292 } },
            { 96000, 1, { 50.0104, 115.1354 }, { 50.1979, 115.5729 } },
            { 96000, 10, { 50.0104, 115.1354 }, { 51.9479, 119.5417 } },
            { 96000, 100, { 50.0104, 115.1354 }, { 68.5312, 156.8646 } },
            { 96000, 1000, { 50.0104, 115.1354 }, { 169.9583, 377.2604 } },
            { 96000, 3000, { 50.0104, 115.1354 }, { 227.6979, 497.2188 } } } },
    };
    return all;
}

constexpr double stepDb = 20.0;

// dyn-lab's AUTO_RELEASE_MULTIPLIER and AUTO_RELEASE_TRANSIENT_MARGIN: the longest release is this many times
// Release, and the shortest burst releases within this fraction of Release.
constexpr double multiplier = 5.0;
constexpr double transientMargin = 0.01;

// The Gain Reduction in dB on each sample of a steady level held for loudSamples STEP_DB over Threshold, then for
// quietSamples STEP_DB under it, through an Engine prepared just before, so the run grid starts with the first sample.
std::vector<double> gainReductionOverBurst (const Settings& settings, double sampleRate, int loudSamples, int quietSamples)
{
    auto loud = test::steadyLevel (1, loudSamples, settings.thresholdDb + stepDb).front();
    const auto quiet = test::steadyLevel (1, quietSamples, settings.thresholdDb - stepDb).front();
    loud.insert (loud.end(), quiet.begin(), quiet.end());
    const auto input = loud;
    test::Channels audio { loud };
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings (settings);
    test::processInBlocks (engine, audio, { 512 });
    return test::gainReductionDb (input, audio.front());
}

// The Release after loudSamples of compression, as dyn-lab measures it: from the Gain Reduction the loud level
// reached, counted from when Release begins.
Release releaseAfter (const Settings& settings, double sampleRate, int loudSamples, double quietSeconds)
{
    const auto reduction = gainReductionOverBurst (settings, sampleRate, loudSamples, static_cast<int> (quietSeconds * sampleRate));
    const auto from = static_cast<std::size_t> (loudSamples);
    const auto c = test::crossings (reduction, reduction[from - 1], 0.0, from);
    REQUIRE (c.ninety);
    const double sampleMs = 1000.0 / sampleRate;
    return { (*c.sixtyThree - c.held) * sampleMs, (*c.ninety - c.held) * sampleMs };
}

// Within one sample of dyn-lab's figure, which it prints rounded to 0.0001 ms. Where a crossing falls exactly on a
// sample (63% at 100 ms is 4410 samples at 44.1 kHz), the float output can tip it either way: one sample, and the
// rounding on top.
void checkWithinOneSample (const Release& measured, const Release& expected, double sampleRate)
{
    const double tolerance = 1000.0 / sampleRate + 0.00005;
    CHECK (std::abs (measured.release63 - expected.release63) <= tolerance);
    CHECK (std::abs (measured.release90 - expected.release90) <= tolerance);
}

} // namespace

TEST_CASE ("After each burst, Release matches dyn-lab's bursts within one sample, Auto Release off and on")
{
    const auto& bursts = GENERATE (from_range (allBursts()));
    const auto& burst = GENERATE_REF (from_range (bursts.rows));
    CAPTURE (bursts.command, burst.sampleRate, burst.lengthMs);
    const int loudSamples = static_cast<int> (std::lround (burst.lengthMs * burst.sampleRate / 1000.0));
    auto settings = bursts.settings;
    settings.autoRelease = false;
    checkWithinOneSample (releaseAfter (settings, burst.sampleRate, loudSamples, 1.0), burst.off, burst.sampleRate);
    settings.autoRelease = true;
    checkWithinOneSample (releaseAfter (settings, burst.sampleRate, loudSamples, 2.0), burst.on, burst.sampleRate);
}

TEST_CASE ("With Auto Release on, Release stays short after a transient and lengthens after sustained compression")
{
    for (const auto& bursts : allBursts())
    {
        CAPTURE (bursts.command);
        const double releaseMs = bursts.settings.releaseMs;
        for (const double sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            CAPTURE (sampleRate);
            auto settings = bursts.settings;
            settings.autoRelease = true;
            // The shortest burst, 1 ms, against Release; the longest, 3 s, against dyn-lab's figure, near 5 times it.
            const auto shortest = releaseAfter (settings, sampleRate, static_cast<int> (std::lround (sampleRate / 1000.0)), 1.0);
            CHECK (shortest.release63 <= releaseMs * (1.0 + transientMargin));
            const auto longest = releaseAfter (settings, sampleRate, static_cast<int> (std::lround (3.0 * sampleRate)), 2.0);
            CHECK (longest.release63 > releaseMs * (multiplier - 1.0));
            CHECK (longest.release63 < releaseMs * multiplier);
        }
    }
}

namespace
{

struct Rate
{
    double sampleRate;
    Release release;
    double holdMs;
};

struct Steps
{
    std::string command; // the dyn-lab command that printed rates; its Attack columns are left out, as unchanged
    Settings settings;
    std::vector<Rate> rates;
};

const std::vector<Steps>& allSteps()
{
    static const std::vector<Steps> all {
        { "python3 tools/dyn-lab/dynlab.py step --auto-release --rates 44100 48000 88200 96000 176400 192000",
          { .autoRelease = true },
          { { 44100, { 456.4626, 952.6531 }, 0.0 },
            { 48000, { 456.4583, 952.6458 }, 0.0 },
            { 88200, { 456.4512, 952.6190 }, 0.0 },
            { 96000, { 456.4479, 952.6146 }, 0.0 },
            { 176400, { 456.4399, 952.6020 }, 0.0 },
            { 192000, { 456.4375, 952.5990 }, 0.0 } } },
        { "python3 tools/dyn-lab/dynlab.py step --auto-release --attack 0.5 --release 50 --hold 20 --rates 44100 48000 96000",
          { .attackMs = 0.5, .releaseMs = 50.0, .autoRelease = true, .holdMs = 20.0 },
          { { 44100, { 236.5986, 515.3741 }, 20.0 },
            { 48000, { 236.6042, 515.3750 }, 20.0 },
            { 96000, { 236.5833, 515.3542 }, 20.0 } } },
    };
    return all;
}

// Long enough over Threshold that the sustain state is full to within what one sample of Release can show (dyn-lab's
// step starts it full), and a whole number of runs at every rate, so the step falls at the start of a run, as it does
// in dyn-lab.
constexpr double sustainedSeconds = 20.0;

} // namespace

TEST_CASE ("With Auto Release on, a step down from steady state matches dyn-lab's step within one sample")
{
    const auto& steps = GENERATE (from_range (allSteps()));
    const auto& rate = GENERATE_REF (from_range (steps.rates));
    CAPTURE (steps.command, rate.sampleRate);
    const int loudSamples = static_cast<int> (sustainedSeconds * rate.sampleRate);
    REQUIRE (loudSamples % 16 == 0);
    const auto reduction = gainReductionOverBurst (steps.settings, rate.sampleRate, loudSamples, static_cast<int> (2.0 * rate.sampleRate));
    const auto from = static_cast<std::size_t> (loudSamples);
    const auto c = test::crossings (reduction, reduction[from - 1], 0.0, from);
    REQUIRE (c.ninety);
    const double sampleMs = 1000.0 / rate.sampleRate;
    CHECK (std::abs (c.held * sampleMs - rate.holdMs) <= sampleMs);
    checkWithinOneSample ({ (*c.sixtyThree - c.held) * sampleMs, (*c.ninety - c.held) * sampleMs }, rate.release, rate.sampleRate);
}
