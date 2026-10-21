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

    std::vector<float> left (blockSize, 0.5f), right (blockSize, -0.5f), sidechainSamples (blockSize, 0.25f);
    float* main[] = { left.data(), right.data() };
    const float* sidechain[] = { sidechainSamples.data() };
    const ConstAudioBlock sidechainBlock { sidechain, 1, blockSize };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // Every setting moving every block, Output Gain back to 0 dB every few and Auto Gain on and off; blocks cutting
        // the grid's runs; the Sidechain coming and going.
        engine.setSettings ({ .thresholdDb = -6.0 * (block % 10),
                              .ratio = block % 5 == 4 ? std::numeric_limits<double>::infinity() : 1.0 + block % 5,
                              .kneeDb = 8.0 * (block % 9),
                              .attackMs = 0.005 + 25.0 * (block % 10),
                              .releaseMs = 5.0 + 500.0 * (block % 10),
                              .holdMs = 50.0 * (block % 10),
                              .rangeDb = 6.0 * (block % 11),
                              .autoGain = block % 2 == 0,
                              .outputGainDb = block % 4 == 3 ? 0.0 : 6.0 * (block % 7) - 18.0 });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main, 2, count }, block % 2 == 0 ? &sidechainBlock : nullptr);
    }

    REQUIRE (guard.allocations() == 0);
}
