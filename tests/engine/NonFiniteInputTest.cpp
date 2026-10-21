#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>

using namespace comp1;

TEST_CASE ("A non-finite input sample never reaches the output, which plays on as if it were silence")
{
    const bool autoGain = GENERATE (false, true);
    CAPTURE (autoGain);
    const Settings settings { .autoGain = autoGain, .outputGainDb = 6.0 };
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
