#include "Measure.h"

#include <plugkit/OutputLevel.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace comp1;
using Catch::Matchers::WithinAbs;

namespace
{

// Settings that apply no Gain Reduction: the output is the input.
Settings unity()
{
    return { .ratio = 1.0 };
}

} // namespace

TEST_CASE ("The Output Level reads each channel's sample peak and 300 ms RMS, one channel for a mono Engine")
{
    const int numChannels = GENERATE (1, 2);
    CAPTURE (numChannels);
    Engine engine;
    engine.prepare (48000.0, 512, numChannels);
    engine.setSettings (unity());
    REQUIRE (engine.outputLevelChannels() == numChannels);

    // A square wave at -6 dB: its sample peak and its RMS both read -6 dB, once it fills the 300 ms window.
    auto audio = test::steadyLevel (numChannels, 24000, -6.0);
    test::processInBlocks (engine, audio, { 512 });
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto level = engine.readOutputLevel (ch);
        CHECK_THAT (level.peakDb, WithinAbs (-6.0, 1.0e-4));
        CHECK_THAT (level.rmsDb, WithinAbs (-6.0, 1.0e-4));
        // Reading resets the peak, not the RMS.
        const auto again = engine.readOutputLevel (ch);
        CHECK (again.peakDb == plugkit::levelFloorDb);
        CHECK_THAT (again.rmsDb, WithinAbs (-6.0, 1.0e-4));
    }
    // A channel the Engine doesn't have reads the floor.
    CHECK (engine.readOutputLevel (numChannels).peakDb == plugkit::levelFloorDb);
}
