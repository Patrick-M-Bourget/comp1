#include "Measure.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace comp1;

namespace
{
// Kept out of the optimiser's reach, so arithmetic on it happens at run time, in the thread's mode.
volatile float smallestNormal = std::numeric_limits<float>::min();
} // namespace

TEST_CASE ("process() flushes subnormal numbers to zero and leaves the caller's floating-point mode as it found it")
{
    const float subnormal = smallestNormal / 4.0f;
    REQUIRE (std::fpclassify (subnormal) == FP_SUBNORMAL);

    const std::vector<float> samples (64, subnormal);
    test::Channels audio { samples, samples };
    Engine engine;
    engine.prepare (48000.0, 64, 2);
    engine.setSettings ({ .outputGainDb = -6.0 });
    test::processInBlocks (engine, audio, { 64 });

    for (const auto& channel : audio)
        for (const float sample : channel)
            REQUIRE (sample == 0.0f);
    // The caller's own arithmetic still reaches subnormal numbers.
    CHECK (std::fpclassify (smallestNormal / 4.0f) == FP_SUBNORMAL);
}
