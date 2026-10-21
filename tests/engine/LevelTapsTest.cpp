#include "Measure.h"

#include <plugkit/OutputLevel.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
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

// Every record the level history holds, oldest first.
std::vector<LevelRecord> readAll (Engine& engine)
{
    std::vector<LevelRecord> records (static_cast<std::size_t> (engine.levelHistoryCapacity()));
    records.resize (static_cast<std::size_t> (engine.readLevels (records.data(), static_cast<int> (records.size()))));
    return records;
}

double db (double amplitude)
{
    return 20.0 * std::log10 (amplitude);
}

} // namespace

TEST_CASE ("The level history holds a record per completed run, with the run's peak Input Level and Output Level")
{
    const int numChannels = GENERATE (1, 2);
    CAPTURE (numChannels);
    Engine engine;
    engine.prepare (44100.0, 512, numChannels);
    engine.setSettings (unity());

    // Run r peaks at 0.9^r, at one sample on channel r % numChannels, with silence elsewhere; a run whose r is a
    // multiple of 7 holds a NaN there instead, which plays as silence, so it reads the floor. Then five samples of a
    // run left open, which makes no record.
    constexpr int runs = 40;
    test::Channels audio (static_cast<std::size_t> (numChannels), std::vector<float> (runs * 16 + 5));
    std::vector<double> expectedDb;
    for (int r = 0; r < runs; ++r)
    {
        auto& channel = audio[static_cast<std::size_t> (r % numChannels)];
        const auto at = static_cast<std::size_t> (r * 16 + (r * 5) % 16);
        if (r % 7 == 0)
        {
            channel[at] = std::numeric_limits<float>::quiet_NaN();
            expectedDb.push_back (plugkit::levelFloorDb);
        }
        else
        {
            const auto peak = static_cast<float> (std::pow (0.9, r));
            channel[at] = r % 2 == 0 ? peak : -peak;
            expectedDb.push_back (db (static_cast<double> (peak)));
        }
    }
    audio.front().back() = 1.0f; // in the open run
    test::processInBlocks (engine, audio, { 512 });

    const auto records = readAll (engine);
    REQUIRE (records.size() == static_cast<std::size_t> (runs));
    for (std::size_t r = 0; r < records.size(); ++r)
    {
        CAPTURE (r);
        CHECK_THAT (records[r].inputDb, WithinAbs (expectedDb[r], 1.0e-4));
        CHECK_THAT (records[r].outputDb, WithinAbs (expectedDb[r], 1.0e-4));
    }
    // A history that has been read is empty until more runs complete.
    CHECK (readAll (engine).empty());
}

TEST_CASE ("The level history holds at least 150 ms of runs at every sample rate")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, 512, 2);
    CHECK (engine.levelHistoryCapacity() * 16 >= 0.15 * sampleRate);
}

TEST_CASE ("A full level history drops new records, and a reader that skips reads only what came after")
{
    Engine engine;
    engine.prepare (48000.0, 512, 1);
    engine.setSettings (unity());
    const int capacity = engine.levelHistoryCapacity();
    // Each run's level says when it was played: run r peaks at 2^-(r % 24), above the floor.
    const auto play = [&] (int fromRun, int runs) {
        test::Channels audio (1, std::vector<float> (static_cast<std::size_t> (runs) * 16));
        for (int r = 0; r < runs; ++r)
            audio.front()[static_cast<std::size_t> (r) * 16] = std::ldexp (1.0f, -((fromRun + r) % 24));
        test::processInBlocks (engine, audio, { 512 });
    };
    const auto runAt = [] (const LevelRecord& record) { return static_cast<int> (std::lround (-record.inputDb / db (2.0))); };

    // Nothing reads, as while the editor is closed: the history keeps the first capacity runs.
    play (0, capacity + 100);
    auto records = readAll (engine);
    REQUIRE (records.size() == static_cast<std::size_t> (capacity));
    for (int r = 0; r < capacity; ++r)
        REQUIRE (runAt (records[static_cast<std::size_t> (r)]) == r % 24);

    play (0, capacity);
    engine.skipLevels();
    play (5, 10);
    records = readAll (engine);
    REQUIRE (records.size() == 10);
    for (int r = 0; r < 10; ++r)
        CHECK (runAt (records[static_cast<std::size_t> (r)]) == 5 + r);
}

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
