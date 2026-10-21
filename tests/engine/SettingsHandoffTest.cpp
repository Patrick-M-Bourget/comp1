#include "Measure.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using namespace comp1;

TEST_CASE ("Settings set on another thread while audio runs reach the output")
{
    constexpr int blockSize = 64;
    Engine engine;
    engine.prepare (48000.0, blockSize, 1);

    // The writer alternates between two Output Gains, then settles on one: while it writes, every sample plays a
    // gain between the two, and once it has stopped, the last one set plays.
    std::atomic<bool> done { false };
    std::thread writer ([&] {
        for (int i = 0; i < 20000; ++i)
            engine.setSettings ({ .outputGainDb = i % 2 == 0 ? -6.0 : 0.0 });
        engine.setSettings ({ .outputGainDb = -6.0 });
        done = true;
    });

    const auto half = static_cast<float> (test::minus6DbGain);
    std::vector<float> samples (blockSize);
    float* channels[] = { samples.data() };
    const auto playBlock = [&] {
        std::fill (samples.begin(), samples.end(), 1.0f);
        engine.process ({ channels, 1, blockSize });
    };
    while (! done.load())
    {
        playBlock();
        for (const float sample : samples)
            REQUIRE ((sample >= half && sample <= 1.0f));
    }
    writer.join();
    for (int block = 0; block < 300; ++block) // 400 ms, for the glide to settle
        playBlock();
    CHECK (samples.back() == half);
}
