#include "Measure.h"

#include "comp1/Latency.h"

#include <plugkit/test/AllocationGuard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <optional>
#include <thread>
#include <vector>

using namespace comp1;

namespace
{

// The mean of audio's channels, sample for sample, worked out here: the mono signal the tap carries.
std::vector<float> meanOf (const test::Channels& audio)
{
    std::vector<float> mean (audio.front().size());
    for (std::size_t i = 0; i < mean.size(); ++i)
    {
        double sum = 0.0;
        for (const auto& channel : audio)
            sum += static_cast<double> (channel[i]);
        mean[i] = static_cast<float> (sum / static_cast<double> (audio.size()));
    }
    return mean;
}

// Runs audio through engine in place, cut into blocks of the sizes given, in turn and repeating, with the samples of
// sidechainSamples on the Sidechain when it isn't null, reading the detection source tap after each block, as the
// editor reads it while the host plays; from change's sample on, with its settings, when there is one. Returns every
// sample read.
std::vector<float> playAndRead (Engine& engine,
                                test::Channels audio,
                                const std::vector<int>& blockSizes,
                                const test::Channels* sidechainSamples = nullptr,
                                const std::optional<test::SettingsChange>& change = std::nullopt)
{
    std::vector<float> all, samples (static_cast<std::size_t> (engine.detectionSourceSampleCapacity()));
    const int numSamples = static_cast<int> (audio.front().size());
    std::vector<float*> main (audio.size());
    std::vector<const float*> sidechain (sidechainSamples != nullptr ? sidechainSamples->size() : 0);
    for (int start = 0, block = 0; start < numSamples; ++block)
    {
        if (change && start == change->at)
            engine.setSettings (change->settings);
        int count = std::min (blockSizes[static_cast<std::size_t> (block) % blockSizes.size()], numSamples - start);
        if (change && start < change->at)
            count = std::min (count, change->at - start);
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            main[ch] = audio[ch].data() + start;
        for (std::size_t ch = 0; ch < sidechain.size(); ++ch)
            sidechain[ch] = (*sidechainSamples)[ch].data() + start;
        const ConstAudioBlock sidechainBlock { sidechain.data(), static_cast<int> (sidechain.size()), count };
        engine.process ({ main.data(), static_cast<int> (main.size()), count }, sidechainSamples != nullptr ? &sidechainBlock : nullptr);
        start += count;
        const int read = engine.readDetectionSourceSamples (samples.data(), static_cast<int> (samples.size()));
        all.insert (all.end(), samples.begin(), samples.begin() + read);
    }
    return all;
}

// What the tap carries for audio through a fresh Engine at 48 kHz with settings, read as playAndRead() reads it.
std::vector<float> tapOf (const Settings& settings,
                          const test::Channels& audio,
                          const test::Channels* sidechainSamples = nullptr,
                          const std::vector<int>& blockSizes = { 512 })
{
    Engine engine;
    engine.prepare (48000.0, 4096, static_cast<int> (audio.size()));
    engine.setSettings (settings);
    return playAndRead (engine, audio, blockSizes, sidechainSamples);
}

// signal delayed by samples, the start filled with silence and the end cut off.
std::vector<float> delayed (const std::vector<float>& signal, int samples)
{
    return test::delayed ({ signal }, samples).front();
}

// The Detection EQ's High-pass on at 1 kHz, which the tap, ahead of it, never hears.
Settings withHighPassAt1kHz (Settings settings)
{
    settings.detectionEq.highPass.on = true;
    settings.detectionEq.highPass.filter.frequency = 1000.0;
    return settings;
}

} // namespace

TEST_CASE ("With Detection Source Internal the tap carries the mean of the main input's channels, before the Detection EQ")
{
    const int numChannels = GENERATE (1, 2);
    const bool highPassOn = GENERATE (false, true);
    CAPTURE (numChannels, highPassOn);
    const auto input = test::noise (numChannels, 4800, 7);
    const Settings settings = highPassOn ? withHighPassAt1kHz ({}) : Settings {};
    CHECK (tapOf (settings, input) == meanOf (input));

    // In High Quality the detection stage takes the input as the Before section has delayed it.
    auto highQuality = settings;
    highQuality.processingMode = ProcessingMode::HighQuality;
    CHECK (tapOf (highQuality, input) == delayed (meanOf (input), oversamplingSectionSamples));
}

TEST_CASE ("With Detection Source External the tap carries the mean of the Sidechain's channels, and silence with none")
{
    const int numChannels = GENERATE (1, 2);
    const int sidechainChannels = GENERATE (1, 2);
    CAPTURE (numChannels, sidechainChannels);
    const auto input = test::noise (numChannels, 4800, 3);
    const auto sidechain = test::noise (sidechainChannels, 4800, 11);
    Settings external = withHighPassAt1kHz ({ .detectionSource = DetectionSource::External });
    CHECK (tapOf (external, input, &sidechain) == meanOf (sidechain));

    auto highQuality = external;
    highQuality.processingMode = ProcessingMode::HighQuality;
    CHECK (tapOf (highQuality, input, &sidechain) == delayed (meanOf (sidechain), oversamplingSectionSamples));

    // With no Sidechain connected, exact silence.
    const auto none = tapOf (external, input);
    REQUIRE (none.size() == input.front().size());
    CHECK (std::all_of (none.begin(), none.end(), [] (float s) { return s == 0.0f; }));
}

TEST_CASE ("The tap carries one sample per input sample in both Processing Modes and across a Processing Mode switch")
{
    const auto input = test::noise (2, 48000, 5);
    for (const auto from : { ProcessingMode::ZeroLatency, ProcessingMode::HighQuality })
    {
        const auto to = from == ProcessingMode::ZeroLatency ? ProcessingMode::HighQuality : ProcessingMode::ZeroLatency;
        CAPTURE (from);
        Engine engine;
        engine.prepare (48000.0, 512, 2);
        engine.setSettings ({ .processingMode = from });
        // The switch at 0.1 s fades out and in again well inside the second that is played.
        const auto read = playAndRead (engine, input, { 333 }, nullptr, test::SettingsChange { .at = 4800, .settings = { .processingMode = to } });
        CHECK (read.size() == input.front().size());
        // Before the switch, the input as the detection stage takes it in the mode playing.
        const auto mean = meanOf (input);
        const auto expected = from == ProcessingMode::HighQuality ? delayed (mean, oversamplingSectionSamples) : mean;
        CHECK (std::equal (read.begin(), read.begin() + 4800, expected.begin()));
    }
}

TEST_CASE ("The tap is the same, sample for sample, however the host cuts the audio into blocks", "[sweep]")
{
    const auto input = test::noise (2, 48000, 9);
    const auto sidechain = test::noise (2, 48000, 13);
    for (const auto mode : { ProcessingMode::ZeroLatency, ProcessingMode::HighQuality })
        for (const auto source : { DetectionSource::Internal, DetectionSource::External })
        {
            CAPTURE (mode, source);
            const Settings settings { .detectionSource = source, .processingMode = mode };
            const auto reference = tapOf (settings, input, &sidechain);
            REQUIRE (reference.size() == input.front().size());
            for (const auto& cut : test::blockCuts())
            {
                CAPTURE (cut);
                CHECK (tapOf (settings, input, &sidechain, cut) == reference);
            }
        }
}

TEST_CASE ("The tap holds at least 150 ms of samples at every sample rate")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    Engine engine;
    engine.prepare (sampleRate, 512, 2);
    CHECK (engine.detectionSourceSampleCapacity() >= 0.15 * sampleRate);
}

TEST_CASE ("A full tap drops new samples, and a reader that skips reads only what came after")
{
    Engine engine;
    engine.prepare (48000.0, 512, 1);
    engine.setSettings ({});
    const int capacity = engine.detectionSourceSampleCapacity();
    // Sample n of what is played says when it was played: n / 2^20, exact in float.
    const auto play = [&] (int from, int count) {
        test::Channels audio (1, std::vector<float> (static_cast<std::size_t> (count)));
        for (int n = 0; n < count; ++n)
            audio.front()[static_cast<std::size_t> (n)] = std::ldexp (static_cast<float> (from + n), -20);
        test::processInBlocks (engine, audio, { 512 });
    };
    const auto readAll = [&] {
        std::vector<float> samples (static_cast<std::size_t> (capacity));
        samples.resize (static_cast<std::size_t> (engine.readDetectionSourceSamples (samples.data(), capacity)));
        return samples;
    };
    const auto at = [] (float sample) { return static_cast<int> (std::ldexp (sample, 20)); };

    // Nothing reads, as while the editor is closed: the tap keeps the first capacity samples.
    play (0, capacity + 1000);
    auto samples = readAll();
    REQUIRE (samples.size() == static_cast<std::size_t> (capacity));
    for (int n = 0; n < capacity; ++n)
        REQUIRE (at (samples[static_cast<std::size_t> (n)]) == n);

    play (0, capacity);
    engine.skipDetectionSourceSamples();
    play (5, 100);
    samples = readAll();
    REQUIRE (samples.size() == 100);
    for (int n = 0; n < 100; ++n)
        CHECK (at (samples[static_cast<std::size_t> (n)]) == 5 + n);
}

TEST_CASE ("The tap is read on a reader thread while process() runs, and reading never allocates")
{
    // The audio thread plays a ramp that counts the samples played, one channel, until the reader has read a second
    // of it, or for ten minutes of audio, when the test fails. Every sample read is the next one played: what the
    // reader gets stays in order, with nothing dropped while it keeps up.
    Engine engine;
    engine.prepare (48000.0, 64, 1);
    engine.setSettings ({});
    std::atomic<bool> done { false };
    std::thread audio ([&] {
        std::vector<float> block (64);
        float* main[] = { block.data() };
        int played = 0;
        for (int n = 0; n < 48000 * 600 / 64 && ! done; ++n)
        {
            for (auto& sample : block)
                sample = std::ldexp (static_cast<float> (played++ % (1 << 20)), -20);
            engine.process ({ main, 1, 64 });
        }
        done = true;
    });

    std::vector<float> samples (static_cast<std::size_t> (engine.detectionSourceSampleCapacity()));
    std::size_t allocations = 0;
    int read = 0;
    bool inOrder = true;
    while (! done && read < 48000)
    {
        plugkit::test::AllocationGuard guard;
        const int count = engine.readDetectionSourceSamples (samples.data(), static_cast<int> (samples.size()));
        for (int i = 0; i < count; ++i)
            inOrder = inOrder && static_cast<int> (std::ldexp (samples[static_cast<std::size_t> (i)], 20)) == (read + i) % (1 << 20);
        read += count;
        allocations += guard.allocations();
    }
    done = true;
    audio.join();

    CHECK (read >= 48000);
    CHECK (allocations == 0);
    CHECK (inOrder);
}

TEST_CASE ("The tap can be read while the host prepares the Engine again at another rate or channel count")
{
    Engine engine;
    engine.prepare (48000.0, 256, 2);
    std::atomic<bool> done { false };
    std::thread reader ([&] {
        std::vector<float> samples (1 << 16);
        while (! done)
        {
            engine.readDetectionSourceSamples (samples.data(), std::min (engine.detectionSourceSampleCapacity(), static_cast<int> (samples.size())));
            engine.skipDetectionSourceSamples();
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
    CHECK (engine.detectionSourceSampleCapacity() >= 0.15 * 192000.0);
}
