// The Timing Response (comp1/TimingResponse.h): the Gain Reduction its detector-only simulation gives a test burst,
// against what the whole Engine applies to the same burst.

#include "Measure.h"
#include "comp1/TimingResponse.h"

#include <plugkit/test/AllocationGuard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

} // namespace

TEST_CASE ("The Timing Response's times run on a log axis from 5 us to about 10 s")
{
    CHECK (TimingResponse::timeMsAt (0) == TimingResponse::earliestMs);
    CHECK_THAT (TimingResponse::earliestMs, WithinAbs (0.005, 1.0e-12));
    CHECK_THAT (TimingResponse::timeMsAt (TimingResponse::points - 1), WithinAbs (10000.0, 50.0));
    for (int i = 1; i < TimingResponse::points; ++i)
        CHECK_THAT (TimingResponse::timeMsAt (i) / TimingResponse::timeMsAt (i - 1), WithinAbs (std::pow (10.0, 1.0 / TimingResponse::pointsPerDecade), 1.0e-9));
}

TEST_CASE ("The Timing Response is the Gain Reduction the whole Engine applies to its test burst and after it, on every Character, within 0.001 dB", "[response]")
{
    const double sampleRate = GENERATE (44100.0, 192000.0);
    const auto character = GENERATE (from_range (test::characters())).character;
    for (const auto& settings : settingsFor (character))
    {
        CAPTURE (sampleRate, static_cast<int> (character), settings.attackMs, settings.releaseMs, settings.holdMs, settings.autoRelease, settings.compFollow);
        const auto response = timingResponse (settings, sampleRate);

        // The burst: Threshold + 20 dB, the played Threshold's, as the Transfer Curve Graph draws it.
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
            CHECK_THAT (response.recovery[static_cast<std::size_t> (i)], WithinAbs (reduction[m], toleranceDb));
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
