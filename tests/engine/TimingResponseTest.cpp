// The Timing Response (comp1/TimingResponse.h): the Gain Reduction its detector-only simulation gives a test burst,
// against what the whole Engine applies to the same burst.

#include "Measure.h"
#include "comp1/TimingResponse.h"

#include <plugkit/test/AllocationGuard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace comp1;
using Catch::Matchers::WithinAbs;

namespace
{

// How far the simulation may be from the Engine, in dB: the Engine's float audio rounds the gain it applies.
constexpr double toleranceDb = 0.001;

// The level that stands in for the silence after the burst when the Engine plays it, in dBFS: so quiet that the
// Transfer Curve asks for nothing at any Threshold, as for silence, yet the gain the Engine applies to it can be read
// off its output.
constexpr double quietDb = -120.0;

// The settings each Character is checked at: the defaults; fast with Hold; slow with Auto Release and a hard Knee;
// ∞:1 with a short Range at a low Threshold; and Comp Follow, which sets the Threshold the burst is over. Colour 0%,
// so the gain the Engine applies is the compressor's alone (Colour changes nothing the detector hears).
std::vector<Settings> settingsFor (Character character)
{
    const Settings base { .character = character, .colour = 0.0 };
    std::vector<Settings> all;
    all.push_back (base);
    auto fast = base;
    fast.attackMs = 0.5;
    fast.releaseMs = 50.0;
    fast.holdMs = 20.0;
    all.push_back (fast);
    auto slow = base;
    slow.attackMs = 30.0;
    slow.releaseMs = 400.0;
    slow.autoRelease = true;
    slow.kneeDb = 0.0;
    all.push_back (slow);
    auto limiting = base;
    limiting.thresholdDb = -40.0;
    limiting.ratio = parameterRanges::inf;
    limiting.rangeDb = 12.0;
    limiting.attackMs = 0.005;
    limiting.releaseMs = 5000.0;
    all.push_back (limiting);
    auto following = base;
    following.compFollow = true;
    following.comp = 80.0;
    all.push_back (following);
    return all;
}

// The lowest and the highest of the sample rates comp1 supports: the response's samples are coarsest at the one and
// most numerous at the other.
std::vector<double> lowestAndHighestRates()
{
    const auto& all = test::sampleRates();
    return { *std::min_element (all.begin(), all.end()), *std::max_element (all.begin(), all.end()) };
}

} // namespace

TEST_CASE ("The Timing Response's times run on a log axis from 5 us to about 10 s")
{
    CHECK (TimingResponse::timeMsAt (0) == TimingResponse::earliestMs);
    CHECK_THAT (TimingResponse::earliestMs, WithinAbs (0.005, 1.0e-12));
    CHECK_THAT (TimingResponse::timeMsAt (TimingResponse::points - 1), WithinAbs (10000.0, 50.0));
    for (int i = 1; i < TimingResponse::points; ++i)
        CHECK_THAT (TimingResponse::timeMsAt (i) / TimingResponse::timeMsAt (i - 1), WithinAbs (std::pow (10.0, 1.0 / TimingResponse::pointsPerDecade), 1.0e-9));
}

TEST_CASE ("The Timing Response is the Gain Reduction the whole Engine applies to its test burst and after it, on every Character, within 0.001 dB; with Auto Release on, the recovery the Engine plays lies within its band", "[response]")
{
    const double sampleRate = GENERATE (from_range (lowestAndHighestRates()));
    const auto character = GENERATE (from_range (test::characters())).character;
    for (const auto& settings : settingsFor (character))
    {
        CAPTURE (sampleRate, static_cast<int> (character), settings.attackMs, settings.releaseMs, settings.holdMs, settings.autoRelease, settings.compFollow);
        const auto response = timingResponse (settings, sampleRate);

        // The burst: Threshold + 20 dB, the played Threshold's, as the Transfer Curve Graph draws it. The played
        // Threshold is worked out here apart from clean::playedCurveOf, which the response reads it from, so a wrong one
        // there shows.
        const double threshold = settings.compFollow ? compThresholdDb (settings.compCurve, settings.comp) : settings.thresholdDb;
        CHECK_THAT (response.burstLevelDb, WithinAbs (std::min (threshold + 20.0, 0.0), 1.0e-12));

        const auto burstSamples = static_cast<int> (std::lround (TimingResponse::burstSeconds * sampleRate));
        const int tailSamples = static_cast<int> (TimingResponse::sampleAt (TimingResponse::points - 1, sampleRate)) + 1;
        auto audio = test::steadyLevel (1, burstSamples, response.burstLevelDb);
        const auto tail = test::steadyLevel (1, tailSamples, quietDb);
        audio[0].insert (audio[0].end(), tail[0].begin(), tail[0].end());
        const auto output = test::played (settings, sampleRate, audio);
        const auto reduction = test::gainReductionDb (audio[0], output[0]);

        for (int i = 0; i < TimingResponse::points; ++i)
        {
            CAPTURE (i, TimingResponse::timeMsAt (i));
            const auto n = static_cast<std::size_t> (std::min<long> (TimingResponse::sampleAt (i, sampleRate), burstSamples - 1));
            CHECK_THAT (response.rise[static_cast<std::size_t> (i)], WithinAbs (reduction[n], toleranceDb));
            const auto m = static_cast<std::size_t> (burstSamples + TimingResponse::sampleAt (i, sampleRate));
            if (! settings.autoRelease)
                CHECK_THAT (response.recovery[static_cast<std::size_t> (i)], WithinAbs (reduction[m], toleranceDb));
            else
            {
                REQUIRE (response.recoveryBand.has_value());
                CHECK (response.recoveryBand->fastest[static_cast<std::size_t> (i)] <= reduction[m] + toleranceDb);
                CHECK (reduction[m] <= response.recoveryBand->slowest[static_cast<std::size_t> (i)] + toleranceDb);
            }
        }
    }
}

TEST_CASE ("The Timing Response's burst is capped at 0 dBFS")
{
    const auto response = timingResponse ({ .thresholdDb = -6.0 }, 48000.0);
    CHECK (response.burstLevelDb == 0.0);
}

TEST_CASE ("The Timing Response reads between its points along log time, holding at its ends")
{
    const auto response = timingResponse ({}, 48000.0);
    const auto i = static_cast<std::size_t> (TimingResponse::points / 2);
    const double a = TimingResponse::timeMsAt (static_cast<int> (i)), b = TimingResponse::timeMsAt (static_cast<int> (i + 1));
    CHECK (response.riseAt (a) == response.rise[i]);
    CHECK_THAT (response.riseAt (std::sqrt (a * b)), WithinAbs ((response.rise[i] + response.rise[i + 1]) / 2.0, 1.0e-9));
    CHECK_THAT (response.recoveryAt (std::sqrt (a * b)), WithinAbs ((response.recovery[i] + response.recovery[i + 1]) / 2.0, 1.0e-9));
    CHECK (response.riseAt (0.0) == response.rise.front());
    CHECK (response.recoveryAt (1.0e9) == response.recovery.back());
}

TEST_CASE ("Working out the Timing Response never allocates")
{
    const Settings settings { .character = Character::Opto, .autoRelease = true };
    TimingResponse response;
    {
        plugkit::test::AllocationGuard guard;
        response = timingResponse (settings, 48000.0);
    }
    CHECK (response.rise.back() > 0.0);
}

TEST_CASE ("With Auto Release on, the Timing Response's recovery band runs from the recovery at Release to the recovery at 5 x Release, and its recovery is the band's fastest edge; with it off there is none", "[response]")
{
    const double sampleRate = GENERATE (from_range (lowestAndHighestRates()));
    const auto character = GENERATE (from_range (test::characters())).character;
    for (auto settings : settingsFor (character))
    {
        settings.autoRelease = false;
        CAPTURE (sampleRate, static_cast<int> (character), settings.attackMs, settings.releaseMs, settings.holdMs);
        CHECK_FALSE (timingResponse (settings, sampleRate).recoveryBand.has_value());

        const auto atRelease = timingResponse (settings, sampleRate);
        auto longest = settings;
        longest.releaseMs = 5.0 * settings.releaseMs;
        const auto atLongest = timingResponse (longest, sampleRate);
        settings.autoRelease = true;
        const auto response = timingResponse (settings, sampleRate);
        REQUIRE (response.recoveryBand.has_value());
        const auto& band = *response.recoveryBand;
        for (std::size_t i = 0; i < TimingResponse::points; ++i)
        {
            CAPTURE (i, TimingResponse::timeMsAt (static_cast<int> (i)));
            CHECK_THAT (band.fastest[i], WithinAbs (atRelease.recovery[i], toleranceDb));
            CHECK_THAT (band.slowest[i], WithinAbs (atLongest.recovery[i], toleranceDb));
            CHECK (response.recovery[i] == band.fastest[i]);
        }
    }
}

TEST_CASE ("On Clean the Timing Response's recovery band's edges are 1/e of the way back at Hold plus Release and at Hold plus 5 x Release")
{
    const Settings settings { .character = Character::Clean, .releaseMs = 80.0, .autoRelease = true, .holdMs = 30.0 };
    const auto response = timingResponse (settings, 48000.0);
    REQUIRE (response.recoveryBand.has_value());
    const double most = response.rise.back();
    const auto edgeAt = [] (const std::array<double, TimingResponse::points>& values, double ms) {
        TimingResponse r;
        r.recovery = values;
        return r.recoveryAt (ms);
    };
    CHECK_THAT (edgeAt (response.recoveryBand->fastest, 30.0 + 80.0) / most, WithinAbs (std::exp (-1.0), 0.01));
    CHECK_THAT (edgeAt (response.recoveryBand->slowest, 30.0 + 400.0) / most, WithinAbs (std::exp (-1.0), 0.01));
}

TEST_CASE ("On Opto the Timing Response's rise band runs from the rise at 0.5 x Attack to the rise at 2 x Attack, the overshoot's scale held at each end, and the rise lies within it; on the other Characters there is none", "[response]")
{
    const double sampleRate = GENERATE (from_range (lowestAndHighestRates()));
    for (const auto& style : test::characters())
    {
        if (style.character != Character::Opto)
        {
            CHECK_FALSE (timingResponse ({ .character = style.character }, sampleRate).riseBand.has_value());
            continue;
        }
        for (const auto& settings : settingsFor (Character::Opto))
        {
            CAPTURE (sampleRate, settings.attackMs, settings.releaseMs, settings.autoRelease, settings.compFollow);
            const auto response = timingResponse (settings, sampleRate);
            REQUIRE (response.riseBand.has_value());
            // With the scale held, Opto attacks as FET does: its loop, both parts at one pole (Opto.h).
            auto held = settings;
            held.character = Character::FET;
            held.attackMs = 0.5 * settings.attackMs;
            const auto fastest = timingResponse (held, sampleRate);
            held.attackMs = 2.0 * settings.attackMs;
            const auto slowest = timingResponse (held, sampleRate);
            for (std::size_t i = 0; i < TimingResponse::points; ++i)
            {
                CAPTURE (i, TimingResponse::timeMsAt (static_cast<int> (i)));
                CHECK_THAT (response.riseBand->fastest[i], WithinAbs (fastest.rise[i], toleranceDb));
                CHECK_THAT (response.riseBand->slowest[i], WithinAbs (slowest.rise[i], toleranceDb));
                CHECK (response.riseBand->slowest[i] <= response.rise[i] + toleranceDb);
                CHECK (response.rise[i] <= response.riseBand->fastest[i] + toleranceDb);
            }
        }
    }
}

TEST_CASE ("On Opto the Timing Response marks the slow part's times, Hold plus 500 ms to Hold plus 5000 ms, which Release doesn't move; on the other Characters none")
{
    for (const auto& style : test::characters())
    {
        CAPTURE (static_cast<int> (style.character));
        for (const double releaseMs : { 20.0, 2000.0 })
        {
            const auto response = timingResponse ({ .character = style.character, .releaseMs = releaseMs, .holdMs = 40.0 }, 48000.0);
            if (style.character != Character::Opto)
            {
                CHECK_FALSE (response.slowPartMs.has_value());
                continue;
            }
            REQUIRE (response.slowPartMs.has_value());
            CHECK_THAT (response.slowPartMs->first, WithinAbs (540.0, 1.0e-9));
            CHECK_THAT (response.slowPartMs->second, WithinAbs (5040.0, 1.0e-9));
        }
    }
}
