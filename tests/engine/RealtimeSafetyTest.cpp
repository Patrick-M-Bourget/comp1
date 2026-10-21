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
        // Every setting moving every block, Output Gain back to 0 dB every few, Auto Release and Auto Gain on and off,
        // Detection Source, Detection Audition and Stereo Placement moving every few, Stereo Link every block; blocks
        // cutting the grid's runs; the Sidechain coming and going, and changing between mono and stereo.
        engine.setSettings ({ .thresholdDb = -6.0 * (block % 10),
                              .ratio = block % 5 == 4 ? std::numeric_limits<double>::infinity() : 1.0 + block % 5,
                              .kneeDb = 8.0 * (block % 9),
                              .attackMs = 0.005 + 25.0 * (block % 10),
                              .releaseMs = 5.0 + 500.0 * (block % 10),
                              .autoRelease = block % 3 != 0,
                              .holdMs = 50.0 * (block % 10),
                              .rangeDb = 6.0 * (block % 11),
                              .autoGain = block % 2 == 0,
                              .outputGainDb = block % 4 == 3 ? 0.0 : 6.0 * (block % 7) - 18.0,
                              .detectionSource = block % 6 < 3 ? DetectionSource::External : DetectionSource::Internal,
                              .stereoPlacement = static_cast<StereoPlacement> ((block / 4) % 3),
                              .stereoLink = 12.5 * (block % 9),
                              .detectionAudition = block % 5 < 2 });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main, 2, count }, sidechains[(block / 2) % 3]);
    }

    REQUIRE (guard.allocations() == 0);
}
