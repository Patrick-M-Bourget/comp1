#include "Measure.h"

#include <plugkit/OutputLevel.h>
#include <plugkit/test/AllocationGuard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <thread>
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

// Runs audio through engine in place as test::processInBlocks does, reading the level history after each block, as
// the editor reads it while the host plays, and returns every record read.
std::vector<LevelRecord> playAndRead (Engine& engine, test::Channels& audio, const std::vector<int>& blockSizes)
{
    std::vector<LevelRecord> all;
    std::vector<LevelRecord> records (static_cast<std::size_t> (engine.levelHistoryCapacity()));
    const int numSamples = static_cast<int> (audio.front().size());
    std::vector<float*> main (audio.size());
    for (int start = 0, block = 0; start < numSamples; ++block)
    {
        const int count = std::min (blockSizes[static_cast<std::size_t> (block) % blockSizes.size()], numSamples - start);
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            main[ch] = audio[ch].data() + start;
        engine.process ({ main.data(), static_cast<int> (main.size()), count });
        start += count;
        const int read = engine.readLevels (records.data(), static_cast<int> (records.size()));
        all.insert (all.end(), records.begin(), records.begin() + read);
    }
    return all;
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

TEST_CASE ("The Detection Level reads 0 dB for a full-scale sine and the floor for silence")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings ({});

    // 100 ms of silence, then a second of a full-scale 1 kHz sine: its loudest Detection Level over the second, many
    // cycles, has met the sine's peak at some sample to within far less than 0.01 dB.
    const auto silent = static_cast<int> (0.1 * sampleRate);
    test::Channels audio (1, std::vector<float> (static_cast<std::size_t> (silent + static_cast<int> (sampleRate))));
    for (std::size_t i = static_cast<std::size_t> (silent); i < audio.front().size(); ++i)
        audio.front()[i] = static_cast<float> (std::sin (2.0 * 3.141592653589793 * 1000.0 * static_cast<double> (i) / sampleRate));

    const auto records = playAndRead (engine, audio, { 512 });
    REQUIRE (records.size() == audio.front().size() / 16);
    double loudest = plugkit::levelFloorDb;
    for (std::size_t run = 0; run < records.size(); ++run)
    {
        if (run * 16 + 16 <= static_cast<std::size_t> (silent))
            REQUIRE (records[run].detectionLevelDb == plugkit::levelFloorDb);
        loudest = std::max (loudest, records[run].detectionLevelDb);
    }
    CHECK_THAT (loudest, WithinAbs (0.0, 0.01));
}

namespace
{

// The level history of half a second of a steady level on each channel, through an Engine with settings.
std::vector<LevelRecord> steadyRecords (const Settings& settings, const std::vector<double>& levelsDb, double sampleRate = 48000.0)
{
    const auto numChannels = static_cast<int> (levelsDb.size());
    const auto length = static_cast<int> (0.5 * sampleRate);
    test::Channels audio;
    for (const double level : levelsDb)
        audio.push_back (test::steadyLevel (1, length, level).front());
    Engine engine;
    engine.prepare (sampleRate, 512, numChannels);
    engine.setSettings (settings);
    return playAndRead (engine, audio, { 512 });
}

} // namespace

TEST_CASE ("At a steady level the Gain Reduction is the gain the Engine applies, and dyn-lab's within 0.01 dB")
{
    // A steady input level, and the Gain Reduction dyn-lab gives for it.
    struct Row
    {
        double inputDb;
        double gainReductionDb;
    };
    struct Curve
    {
        std::string command; // the dyn-lab command that printed rows
        Settings settings;
        std::vector<Row> rows;
    };
    // Rows of each command's output, as printed (to 0.01 dB).
    const auto curve = GENERATE (values<Curve> ({
        { "python3 tools/dyn-lab/dynlab.py transfer", {}, { { -24.0, 0.0 }, { -18.0, 0.56 }, { -12.0, 4.50 }, { 0.0, 13.50 } } },
        { "python3 tools/dyn-lab/dynlab.py transfer --ratio inf --knee 0",
          { .ratio = std::numeric_limits<double>::infinity(), .kneeDb = 0.0 },
          { { -21.0, 0.0 }, { -15.0, 3.00 }, { -6.0, 12.00 }, { 0.0, 18.00 } } },
    }));
    CAPTURE (curve.command);
    for (const auto& row : curve.rows)
    {
        CAPTURE (row.inputDb);
        const auto records = steadyRecords (curve.settings, { row.inputDb });
        // Once Attack has settled, each run's Gain Reduction is how far its Output Level is under its Input Level.
        for (std::size_t r = records.size() / 2; r < records.size(); ++r)
        {
            CAPTURE (r);
            CHECK_THAT (records[r].gainReductionDb, WithinAbs (records[r].inputDb - records[r].outputDb, 0.001));
            CHECK_THAT (records[r].detectionLevelDb, WithinAbs (row.inputDb, 1.0e-4));
        }
        CHECK_THAT (records.back().gainReductionDb, WithinAbs (row.gainReductionDb, 0.01));
    }
}

TEST_CASE ("On FET the Detection Level is the detection signal's, before the feedback, while its Gain Reduction is its own")
{
    // FET's loop compares the level less the Gain Reduction with Threshold, but what the level taps report is the
    // detection signal's level: Clean's, record for record, on the same noise. FET's faster attack moves its Gain
    // Reduction off Clean's.
    const auto input = test::noise (2, 9600);
    const auto clean = test::levelHistory ({ .thresholdDb = -30.0 }, 48000.0, input);
    const auto fet = test::levelHistory ({ .character = Character::FET, .thresholdDb = -30.0 }, 48000.0, input);
    REQUIRE (fet.size() == clean.size());
    bool sameLevels = true, sameReductions = true;
    for (std::size_t r = 0; r < fet.size(); ++r)
    {
        sameLevels = sameLevels && fet[r].detectionLevelDb == clean[r].detectionLevelDb;
        sameReductions = sameReductions && fet[r].gainReductionDb == clean[r].gainReductionDb;
    }
    CHECK (sameLevels);
    CHECK_FALSE (sameReductions);
}

TEST_CASE ("Across channels the Gain Reduction is the larger, and the Detection Level the louder")
{
    // At the defaults, dyn-lab's transfer gives 4.50 dB of Gain Reduction at -12 dB and 13.50 dB at 0 dB.
    for (const auto& levelsDb : { std::vector<double> { -12.0, 0.0 }, std::vector<double> { 0.0, -12.0 } })
    {
        const auto records = steadyRecords ({}, levelsDb);
        CHECK_THAT (records.back().gainReductionDb, WithinAbs (13.50, 0.01));
        CHECK_THAT (records.back().detectionLevelDb, WithinAbs (0.0, 1.0e-4));
        CHECK_THAT (records.back().inputDb, WithinAbs (0.0, 1.0e-4));
    }
}

TEST_CASE ("The Gain Reduction leaves out Auto Gain and Output Gain, which move the Output Level")
{
    struct Gain
    {
        Settings settings;
        double gainDb;
    };
    // Auto Gain at the defaults is `python3 tools/dyn-lab/dynlab.py autogain`'s +3.2470 dB.
    const auto gain = GENERATE (values<Gain> ({ { { .autoGain = true }, 3.2470 }, { { .outputGainDb = 6.0 }, 6.0 }, { { .outputGainDb = -9.0 }, -9.0 } }));
    CAPTURE (gain.gainDb);
    const auto plain = steadyRecords ({}, { -6.0 });
    const auto moved = steadyRecords (gain.settings, { -6.0 });
    REQUIRE (moved.size() == plain.size());
    for (std::size_t r = 0; r < plain.size(); ++r)
    {
        CAPTURE (r);
        REQUIRE (moved[r].gainReductionDb == plain[r].gainReductionDb);
        REQUIRE (moved[r].detectionLevelDb == plain[r].detectionLevelDb);
    }
    // Once the gain has glided there.
    CHECK_THAT (moved.back().outputDb - plain.back().outputDb, WithinAbs (gain.gainDb, 0.001));
}

TEST_CASE ("On a step the Gain Reduction records follow the Engine's own step response, run for run")
{
    // dyn-lab's `step` at the defaults: from 20 dB under Threshold to 20 dB over it, and back, each from steady state.
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (sampleRate);
    const auto length = static_cast<int> (0.5 * sampleRate) / 16 * 16;
    auto input = test::steadyLevel (1, length, -38.0).front();
    for (const double level : { 2.0, -38.0 })
    {
        const auto more = test::steadyLevel (1, length, level).front();
        input.insert (input.end(), more.begin(), more.end());
    }
    test::Channels audio { input };
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings ({});
    const auto records = playAndRead (engine, audio, { 512 });

    const auto heard = test::gainReductionDb (input, audio.front());
    REQUIRE (records.size() == heard.size() / 16);
    for (std::size_t r = 0; r < records.size(); ++r)
    {
        CAPTURE (r);
        const auto run = heard.begin() + static_cast<std::ptrdiff_t> (r * 16);
        const double most = *std::max_element (run, run + 16);
        // The output is float: a Gain Reduction read off it carries up to about 1e-6 dB of rounding.
        REQUIRE_THAT (records[r].gainReductionDb, WithinAbs (std::max (most, 0.0), 1.0e-5));
    }
}

TEST_CASE ("The level history is the same, record for record, however the host cuts the audio into blocks")
{
    const int numChannels = GENERATE (1, 2);
    CAPTURE (numChannels);
    const auto input = test::noise (numChannels, 48000, 3);
    const auto recordsCutBy = [&] (const std::vector<int>& blockSizes) {
        auto audio = input;
        Engine engine;
        engine.prepare (48000.0, 4096, numChannels);
        engine.setSettings ({ .thresholdDb = -30.0, .autoGain = true });
        return playAndRead (engine, audio, blockSizes);
    };
    const auto reference = recordsCutBy ({ 512 });
    REQUIRE (reference.size() == 3000);
    for (const auto& cut : test::blockCuts())
    {
        const auto records = recordsCutBy (cut);
        REQUIRE (records.size() == reference.size());
        for (std::size_t r = 0; r < records.size(); ++r)
        {
            CAPTURE (cut, r);
            REQUIRE (records[r].inputDb == reference[r].inputDb);
            REQUIRE (records[r].outputDb == reference[r].outputDb);
            REQUIRE (records[r].gainReductionDb == reference[r].gainReductionDb);
            REQUIRE (records[r].detectionLevelDb == reference[r].detectionLevelDb);
        }
    }
}

TEST_CASE ("Each level tap is read on a reader thread while process() runs, and reading never allocates")
{
    // The audio thread plays a steady 0 dB on both channels at the defaults, which dyn-lab's transfer compresses by
    // 13.50 dB. The reader reads every tap until it has seen each one settle there; the audio plays until then, or for
    // ten minutes of audio, when the test fails. The reader starts at once, or once a second has played, as a reader
    // the scheduler holds back would: its first read then spans the onset, before the Gain Reduction has attacked.
    const int samplesBeforeReading = GENERATE (0, 48000);
    CAPTURE (samplesBeforeReading);
    Engine engine;
    engine.prepare (48000.0, 64, 2);
    engine.setSettings ({});
    std::atomic<bool> done { false };
    std::atomic<int> samplesPlayed { 0 };
    std::thread audio ([&] {
        test::Channels block (2, std::vector<float> (64));
        float* main[] = { block[0].data(), block[1].data() };
        for (int n = 0; n < 48000 * 600 / 64 && ! done; ++n)
        {
            for (auto& channel : block)
                for (std::size_t i = 0; i < channel.size(); ++i)
                    channel[i] = i % 2 == 0 ? 1.0f : -1.0f;
            engine.process ({ main, 2, 64 });
            samplesPlayed += 64;
        }
        done = true;
    });
    while (! done && samplesPlayed < samplesBeforeReading)
        std::this_thread::yield();

    std::vector<LevelRecord> records (static_cast<std::size_t> (engine.levelHistoryCapacity()));
    LevelRecord last {};
    plugkit::OutputLevel output[2] {};
    std::size_t allocations = 0;
    const auto settled = [] (double db, double target) { return std::abs (db - target) < 0.01; };
    // A tap's Peak is the largest since its last read, so the one that spans the onset holds it near 0 dB: the reader
    // waits for a Peak that has settled too, not just the RMS.
    const auto outputSettled = [&] (const plugkit::OutputLevel& level) {
        return settled (level.peakDb, -13.50) && settled (level.rmsDb, -13.50);
    };
    while (! done && ! (settled (last.gainReductionDb, 13.50) && outputSettled (output[0]) && outputSettled (output[1])))
    {
        plugkit::test::AllocationGuard guard;
        if (const int count = engine.readLevels (records.data(), static_cast<int> (records.size())); count > 0)
            last = records[static_cast<std::size_t> (count - 1)];
        for (int ch = 0; ch < engine.outputLevelChannels(); ++ch)
            if (const auto level = engine.readOutputLevel (ch); level.peakDb > plugkit::levelFloorDb)
                output[ch] = level;
        allocations += guard.allocations();
    }
    done = true;
    audio.join();

    CHECK (allocations == 0);
    CHECK_THAT (last.inputDb, WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (last.detectionLevelDb, WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (last.gainReductionDb, WithinAbs (13.50, 0.01));
    CHECK_THAT (last.outputDb, WithinAbs (-13.50, 0.01));
    for (const auto& level : output)
    {
        CHECK_THAT (level.peakDb, WithinAbs (-13.50, 0.01));
        CHECK_THAT (level.rmsDb, WithinAbs (-13.50, 0.01));
    }
}

TEST_CASE ("The level taps can be read while the host prepares the Engine again at another rate or channel count")
{
    Engine engine;
    engine.prepare (48000.0, 256, 2);
    std::atomic<bool> done { false };
    std::thread reader ([&] {
        std::vector<LevelRecord> records (4096);
        while (! done)
        {
            engine.readLevels (records.data(), std::min (engine.levelHistoryCapacity(), static_cast<int> (records.size())));
            for (int ch = 0; ch < 2; ++ch)
                engine.readOutputLevel (ch);
            engine.skipLevels();
        }
    });

    // The host prepares and processes on its audio thread, here this one, while the editor reads.
    auto audio = test::noise (2, 256);
    float* main[] = { audio[0].data(), audio[1].data() };
    const double rates[] = { 44100.0, 96000.0, 192000.0, 48000.0 };
    for (int round = 0; round < 60; ++round)
    {
        const int numChannels = 1 + round % 2;
        engine.prepare (rates[round % 4], 256, numChannels);
        for (int block = 0; block < 8; ++block)
            engine.process ({ main, numChannels, 256 });
    }
    done = true;
    reader.join();
    CHECK (engine.outputLevelChannels() == 2);
}
