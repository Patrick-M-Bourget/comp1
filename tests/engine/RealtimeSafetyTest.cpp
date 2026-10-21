#include "AllocationGuard.h"
#include "Signals.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace comp1;

TEST_CASE ("Engine does not allocate while processing or taking new settings")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (48000.0, blockSize, 2);

    std::vector<float> left (blockSize, 0.5f), right (blockSize, -0.5f), key (blockSize, 0.25f);
    float* main[] = { left.data(), right.data() };
    const float* sidechain[] = { key.data() };
    const ConstAudioBlock sidechainBlock { sidechain, 1, blockSize };

    test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // Output Gain moving every block, back to 0 dB every few; blocks cutting the grid's runs; the Sidechain
        // coming and going.
        engine.setSettings ({ .outputGainDb = block % 4 == 3 ? 0.0 : 6.0 * (block % 7) - 18.0 });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main, 2, count }, block % 2 == 0 ? &sidechainBlock : nullptr);
    }

    REQUIRE (guard.allocations() == 0);
}
