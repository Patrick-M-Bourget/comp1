#include "Signals.h"

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

    // The writer alternates between two Output Gains; every block plays one of them, and the
    // last one set plays once the writer stops.
    std::atomic<bool> done { false };
    std::thread writer ([&] {
        for (int i = 0; i < 20000; ++i)
            engine.setSettings ({ .outputGainDb = i % 2 == 0 ? -6.0 : 0.0 });
        engine.setSettings ({ .outputGainDb = -6.0 });
        done = true;
    });

    constexpr float half = 0.501187233627272f; // 10^(-6/20), as the Engine rounds it for an input of 1
    std::vector<float> samples (blockSize);
    float* channels[] = { samples.data() };
    bool lastWasHalf = false;
    for (bool writing = true; writing;)
    {
        writing = ! done.load();
        std::fill (samples.begin(), samples.end(), 1.0f);
        engine.process ({ channels, 1, blockSize });
        REQUIRE ((samples.front() == 1.0f || samples.front() == half));
        lastWasHalf = samples.front() == half;
    }
    writer.join();
    CHECK (lastWasHalf);
}
