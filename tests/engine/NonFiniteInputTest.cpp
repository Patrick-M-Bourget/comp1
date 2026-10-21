#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>
#include <utility>

using namespace comp1;

TEST_CASE ("A non-finite input sample never reaches the output, which plays on as if it were silence")
{
    const bool autoGain = GENERATE (false, true);
    // The Limiter on, at a Ceiling the noise passes: a non-finite sample never reaches its gain computer either.
    const bool limiter = GENERATE (false, true);
    // The Gate on, closing on the noise (its peaks, 0.5, are under the closing level, 6 dB under 0 dB): its key, from
    // a detection stage of its own, hears the scrubbed input too.
    const bool gate = GENERATE (false, true);
    CAPTURE (autoGain, limiter, gate);
    const Settings settings { .gate = { .on = gate, .thresholdDb = 0.0, .releaseMs = 5.0, .holdMs = 0.0 },
                              .autoGain = autoGain,
                              .outputGainDb = 6.0,
                              .limiter = limiter,
                              .ceilingDb = -6.0 };
    const auto input = test::noise (2, 4096);
    // The same audio with non-finite samples in it, on both channels, and with silence in their place.
    auto broken = input, silenced = input;
    for (const std::size_t at : { 100u, 101u, 2000u, 3001u })
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            broken[ch][at] = at % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][at] = 0.0f;
        }
    broken[1][3001] = -std::numeric_limits<float>::infinity();
    for (auto* audio : { &broken, &silenced })
    {
        Engine engine;
        engine.prepare (48000.0, 512, 2);
        engine.setSettings (settings);
        test::processInBlocks (engine, *audio, { 512 });
    }
    CHECK (broken == silenced);
}

TEST_CASE ("A non-finite Sidechain sample never reaches the output, nor sticks in the detector: it is heard as silence")
{
    const int sidechainChannels = GENERATE (1, 2);
    const int mainChannels = GENERATE (1, 2);
    CAPTURE (sidechainChannels, mainChannels);
    const Settings settings { .detectionSource = DetectionSource::External };
    const auto input = test::noise (mainChannels, 4096);
    const auto sidechainSamples = test::noise (sidechainChannels, 4096, 3);
    auto broken = sidechainSamples, silenced = sidechainSamples;
    for (const std::size_t at : { 100u, 101u, 2000u, 3001u })
        for (std::size_t ch = 0; ch < broken.size(); ++ch)
        {
            broken[ch][at] = at % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][at] = 0.0f;
        }
    broken.back()[3001] = -std::numeric_limits<float>::infinity();
    auto fromBroken = input, fromSilenced = input;
    for (auto [audio, sidechain] : { std::pair { &fromBroken, &broken }, std::pair { &fromSilenced, &silenced } })
    {
        Engine engine;
        engine.prepare (48000.0, 512, mainChannels);
        engine.setSettings (settings);
        test::processInBlocks (engine, *audio, { 512 }, sidechain);
    }
    REQUIRE (fromSilenced != input); // compressed
    CHECK (fromBroken == fromSilenced);
}
