#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <plugkit/test/AllocationGuard.h>

#include <limits>
#include <vector>

using namespace comp1;

TEST_CASE ("Engine does not allocate while processing or taking new settings")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (48000.0, blockSize, 2);

    std::vector<float> left (blockSize, 0.5f), right (blockSize, -0.5f), sidechainLeft (blockSize, 0.25f), sidechainRight (blockSize, -1.0f);
    float* main[] = { left.data(), right.data() };
    const float* sidechain[] = { sidechainLeft.data(), sidechainRight.data() };
    const ConstAudioBlock monoSidechain { sidechain, 1, blockSize }, stereoSidechain { sidechain, 2, blockSize };
    const ConstAudioBlock* sidechains[] = { &monoSidechain, nullptr, &stereoSidechain };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // Every setting moving every block, Output Gain back to 0 dB every few, the Gate, Auto Release, Auto Gain,
        // Saturation and the Limiter on and off, Saturation moving Before and After, Detection Source, Detection
        // Audition and Stereo Placement moving every few, Stereo Link and every Detection EQ setting moving every
        // block, each band switching on and off, and changing Shape or Slope; the Character moving every few, Clean,
        // VCA and FET in turn crossfading, with Colour moving every block; blocks cutting the grid's runs; the Sidechain
        // coming and going, and changing between mono and stereo.
        DetectionEq eq;
        eq.highPass.on = block % 4 != 1;
        eq.highPass.filter.frequency = 20.0 + 97.0 * block;
        eq.highPass.filter.slope = 6.0 * (1 + block % 16);
        eq.lowPass.on = block % 3 != 2;
        eq.lowPass.filter.frequency = 20000.0 - 150.0 * block;
        eq.lowPass.filter.slope = 6.0 * (1 + (block * 5) % 16);
        eq.freeBand.on = block % 5 != 3;
        eq.freeBand.filter.shape = shapeOf (static_cast<FreeBandShape> (block % 3));
        eq.freeBand.filter.frequency = 30.0 + 211.0 * block;
        eq.freeBand.filter.gain = -30.0 + block % 61;
        eq.freeBand.filter.q = 0.025 + 0.6 * block;
        engine.setSettings ({ .gate = { .on = block % 4 != 2,
                                        .thresholdDb = -6.0 * (block % 10),
                                        .rangeDb = 6.0 * (block % 11),
                                        .attackMs = 0.005 + 25.0 * (block % 10),
                                        .releaseMs = 5.0 + 500.0 * (block % 10),
                                        .holdMs = 50.0 * (block % 10) },
                              .character = static_cast<Character> ((block / 3) % 4),
                              .thresholdDb = -6.0 * (block % 10),
                              .ratio = block % 5 == 4 ? std::numeric_limits<double>::infinity() : 1.0 + block % 5,
                              .kneeDb = 8.0 * (block % 9),
                              .attackMs = 0.005 + 25.0 * (block % 10),
                              .releaseMs = 5.0 + 500.0 * (block % 10),
                              .autoRelease = block % 3 != 0,
                              .holdMs = 50.0 * (block % 10),
                              .rangeDb = 6.0 * (block % 11),
                              .colour = 12.5 * (block % 9),
                              .autoGain = block % 2 == 0,
                              .saturation = block % 4 != 1,
                              .driveDb = 3.0 * (block % 9),
                              .saturationPosition = block % 6 < 3 ? SaturationPosition::Before : SaturationPosition::After,
                              .outputGainDb = block % 4 == 3 ? 0.0 : 6.0 * (block % 7) - 18.0,
                              .limiter = block % 3 != 1,
                              .ceilingDb = -3.0 * (block % 11),
                              .detectionSource = block % 6 < 3 ? DetectionSource::External : DetectionSource::Internal,
                              .detectionEq = eq,
                              .stereoPlacement = static_cast<StereoPlacement> ((block / 4) % 3),
                              .stereoLink = 12.5 * (block % 9),
                              .detectionAudition = block % 5 < 2 });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main, 2, count }, sidechains[(block / 2) % 3]);
    }

    REQUIRE (guard.allocations() == 0);
}
