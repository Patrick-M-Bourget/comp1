// The input tap and the output tap, which the Spectrum display reads: the main input before any stage, and the Wet
// path directly after the compressor.

#include "Measure.h"

#include "comp1/Latency.h"

#include <plugkit/test/AllocationGuard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

using namespace comp1;
using Catch::Matchers::WithinAbs;

namespace
{

const test::SampleTap inputTap { &Engine::readInputTapSamples, &Engine::skipInputTapSamples, &Engine::inputTapSampleCapacity };
const test::SampleTap outputTap { &Engine::readOutputTapSamples, &Engine::skipOutputTapSamples, &Engine::outputTapSampleCapacity };

std::vector<float> delayed (const std::vector<float>& signal, int samples) { return test::delayed ({ signal }, samples).front(); }

// How far the output tap is behind the input tap in mode at 48 kHz: the Before section and the Lookahead budget in
// High Quality, as the compressor's output is; none in Zero Latency.
int outputTapDelay (ProcessingMode mode)
{
    return mode == ProcessingMode::HighQuality ? oversamplingSectionSamples + lookaheadBudgetSamples (48000.0) : 0;
}

// The compressor taking a known amount off a steady level: Clean, hard knee, Threshold -30 dB and Ratio 4:1, so a
// steady -10 dB, 20 dB over, comes out 5 dB over, 15 dB lower.
Settings compressing (ProcessingMode mode)
{
    return { .thresholdDb = -30.0, .ratio = 4.0, .kneeDb = 0.0, .processingMode = mode };
}
constexpr double steadyInputDb = -10.0, knownGainReductionDb = 15.0;

// How much lower, in dB, the output tap's last sample is than the input tap's at the same place in the signal, for a
// steady level: the gain the compressor applied to it.
double tapGapDb (const std::vector<float>& input, const std::vector<float>& output)
{
    return 20.0 * std::log10 (std::abs (static_cast<double> (input.back())) / std::abs (static_cast<double> (output.back())));
}

} // namespace

TEST_CASE ("The input tap carries the mean of the main input's channels as it arrives, before the Gate and in both Processing Modes")
{
    const int numChannels = GENERATE (1, 2);
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (numChannels, mode);
    const auto input = test::noise (numChannels, 4800, 7);
    // The Gate closing hard on the noise, and the compressor reducing it: neither reaches the input tap.
    Settings settings = compressing (mode);
    settings.gate = { .on = true, .thresholdDb = 0.0, .rangeDb = 60.0 };
    CHECK (test::tapOf (inputTap, settings, input) == test::meanOf (input));
}

TEST_CASE ("The input tap carries a non-finite input sample as the silence it plays as")
{
    auto input = test::noise (2, 4800, 3);
    input[0][100] = std::numeric_limits<float>::quiet_NaN();
    input[1][100] = 0.25f;
    input[1][200] = std::numeric_limits<float>::infinity();
    auto expected = input;
    expected[0][100] = 0.0f;
    expected[1][200] = 0.0f;
    CHECK (test::tapOf (inputTap, {}, input) == test::meanOf (expected));
}

TEST_CASE ("With the compressor reducing a steady level by a known amount, the output tap reads that much lower than the input tap")
{
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    const int numChannels = GENERATE (1, 2);
    CAPTURE (mode, numChannels);
    const auto input = test::steadyLevel (numChannels, 24000, steadyInputDb);
    const auto in = test::tapOf (inputTap, compressing (mode), input);
    const auto out = test::tapOf (outputTap, compressing (mode), input);
    REQUIRE (in.size() == input.front().size());
    REQUIRE (out.size() == in.size());
    CHECK_THAT (tapGapDb (in, out), WithinAbs (knownGainReductionDb, 0.01));
}

TEST_CASE ("With the compressor idle the output tap is the input tap, delayed as the compressor's output is in High Quality")
{
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (mode);
    const auto input = test::noise (2, 9600, 5);
    // Ratio 1:1, and a Threshold over anything the noise reaches: no Gain Reduction either way.
    for (const auto idle : { Settings { .ratio = 1.0, .processingMode = mode }, Settings { .thresholdDb = 0.0, .processingMode = mode } })
    {
        const auto in = test::tapOf (inputTap, idle, input);
        const auto out = test::tapOf (outputTap, idle, input);
        REQUIRE (in.size() == input.front().size());
        REQUIRE (out.size() == in.size());
        CHECK (out == delayed (in, outputTapDelay (mode)));
    }
}

TEST_CASE ("Auto Gain, Output Gain, Dry, Saturation After, Detection Audition and the Limiter leave the output tap alone")
{
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (mode);
    const auto input = test::steadyLevel (2, 24000, steadyInputDb);
    const auto plain = compressing (mode);
    const auto reference = test::tapOf (outputTap, plain, input);
    REQUIRE (reference.size() == input.front().size());
    std::vector<std::pair<const char*, Settings>> changes;
    const auto with = [&] (const char* name, auto change) {
        auto settings = plain;
        change (settings);
        changes.emplace_back (name, settings);
    };
    with ("Auto Gain", [] (Settings& s) { s.autoGain = true; });
    with ("Output Gain", [] (Settings& s) { s.outputGainDb = 12.0; });
    with ("Dry", [] (Settings& s) { s.dryDb = 0.0; });
    with ("Saturation After", [] (Settings& s) {
        s.saturation = true;
        s.driveDb = 24.0;
        s.saturationPosition = SaturationPosition::After;
    });
    with ("Detection Audition", [] (Settings& s) { s.detectionAudition = true; });
    with ("Limiter", [] (Settings& s) {
        s.limiter = true;
        s.ceilingDb = -20.0;
    });
    for (const auto& [name, settings] : changes)
    {
        CAPTURE (name);
        // Each changes what the Engine sends on, so the tap is ahead of it.
        REQUIRE (test::played (settings, 48000.0, input) != test::played (plain, 48000.0, input));
        CHECK (test::tapOf (outputTap, settings, input) == reference);
    }
}

TEST_CASE ("The output tap follows the Gate, at either Gate Position, and Saturation Before")
{
    // With the compressor idle and nothing after it adding anything, in Zero Latency the Engine's output is the
    // compressor's: the tap carries its mean.
    const auto input = test::steadyLevel (2, 24000, steadyInputDb);
    const Settings idle { .ratio = 1.0 };
    auto gated = idle;
    gated.gate = { .on = true, .thresholdDb = 0.0, .rangeDb = 20.0 };
    auto gatedAtPost = gated;
    gatedAtPost.gatePosition = GatePosition::Post;
    auto saturated = idle;
    saturated.saturation = true;
    saturated.driveDb = 24.0;
    saturated.saturationPosition = SaturationPosition::Before;
    for (const auto& settings : { gated, gatedAtPost, saturated })
    {
        const auto output = test::played (settings, 48000.0, input);
        REQUIRE (output != input);
        CHECK (test::tapOf (outputTap, settings, input) == test::meanOf (output));
    }
}

TEST_CASE ("The input and output taps are the same, sample for sample, however the host cuts the audio into blocks", "[sweep]")
{
    const auto input = test::noise (2, 48000, 9);
    for (const auto mode : { ProcessingMode::ZeroLatency, ProcessingMode::HighQuality })
        for (const auto* tap : { &inputTap, &outputTap })
        {
            CAPTURE (mode, tap == &inputTap);
            const auto settings = compressing (mode);
            const auto reference = test::tapOf (*tap, settings, input);
            REQUIRE (reference.size() == input.front().size());
            for (const auto& cut : test::blockCuts())
            {
                CAPTURE (cut);
                CHECK (test::tapOf (*tap, settings, input, nullptr, cut) == reference);
            }
        }
}

TEST_CASE ("The input and output taps each carry one sample per input sample across a Processing Mode switch")
{
    const auto input = test::noise (2, 48000, 5);
    for (const auto* tap : { &inputTap, &outputTap })
    {
        CAPTURE (tap == &inputTap);
        Engine engine;
        engine.prepare (48000.0, 512, 2);
        engine.setSettings ({ .processingMode = ProcessingMode::ZeroLatency });
        const auto read = test::playAndReadTap (engine, *tap, input, { 333 }, nullptr,
                                                test::SettingsChange { .at = 4800, .settings = { .processingMode = ProcessingMode::HighQuality } });
        CHECK (read.size() == input.front().size());
    }
}

TEST_CASE ("The input and output taps hold at least 150 ms of samples at every sample rate, drop new ones when full, and skip")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    for (const auto* tap : { &inputTap, &outputTap })
    {
        CAPTURE (tap == &inputTap);
        Engine engine;
        engine.prepare (sampleRate, 512, 1);
        engine.setSettings ({ .ratio = 1.0 });
        const int capacity = (engine.*tap->capacity)();
        CHECK (capacity >= 0.15 * sampleRate);
        // Sample n of what is played says when it was played: n / 2^20, exact in float.
        const auto play = [&] (int from, int count) {
            test::Channels audio (1, std::vector<float> (static_cast<std::size_t> (count)));
            for (int n = 0; n < count; ++n)
                audio.front()[static_cast<std::size_t> (n)] = std::ldexp (static_cast<float> (from + n), -20);
            test::processInBlocks (engine, audio, { 512 });
        };
        std::vector<float> samples (static_cast<std::size_t> (capacity));
        const auto at = [] (float sample) { return static_cast<int> (std::ldexp (sample, 20)); };

        // Nothing reads: the tap keeps the first capacity samples.
        play (0, capacity + 1000);
        REQUIRE ((engine.*tap->read) (samples.data(), capacity) == capacity);
        CHECK (at (samples.front()) == 0);
        CHECK (at (samples.back()) == capacity - 1);

        play (0, capacity);
        (engine.*tap->skip)();
        play (5, 100);
        REQUIRE ((engine.*tap->read) (samples.data(), capacity) == 100);
        CHECK (at (samples.front()) == 5);
        CHECK (at (samples[99]) == 104);
    }
}

TEST_CASE ("Reading the input and output taps never allocates")
{
    Engine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setSettings (compressing (ProcessingMode::HighQuality));
    auto audio = test::noise (2, 4800, 1);
    test::processInBlocks (engine, audio, { 512 });
    std::vector<float> samples (static_cast<std::size_t> (std::max (engine.inputTapSampleCapacity(), engine.outputTapSampleCapacity())));
    plugkit::test::AllocationGuard guard;
    const int in = engine.readInputTapSamples (samples.data(), static_cast<int> (samples.size()));
    const int out = engine.readOutputTapSamples (samples.data(), static_cast<int> (samples.size()));
    engine.skipInputTapSamples();
    engine.skipOutputTapSamples();
    CHECK (guard.allocations() == 0);
    CHECK (in == 4800);
    CHECK (out == 4800);
}

TEST_CASE ("The input tap is read on a reader thread while process() runs, in order, and reading never allocates")
{
    // The audio thread plays a ramp that counts the samples played, one channel, as fast as it can (up to 2^24 samples,
    // which a float holds exactly), until the reader has read a second of it. The audio thread can run ahead of the
    // reader, which then misses what the full tap drops; but what each read gets is in order with no gap inside it, and
    // each read comes after the one before.
    Engine engine;
    engine.prepare (48000.0, 64, 1);
    engine.setSettings ({});
    std::atomic<bool> done { false };
    std::thread audio ([&] {
        std::vector<float> block (64);
        float* main[] = { block.data() };
        int played = 0;
        while (played + 64 <= (1 << 24) && ! done)
        {
            for (auto& sample : block)
                sample = static_cast<float> (played++);
            engine.process ({ main, 1, 64 });
        }
        done = true;
    });

    std::vector<float> samples (static_cast<std::size_t> (engine.inputTapSampleCapacity()));
    std::size_t allocations = 0;
    int read = 0;
    float last = -1.0f;
    bool inOrder = true;
    while (! done && read < 48000)
    {
        plugkit::test::AllocationGuard guard;
        const int count = engine.readInputTapSamples (samples.data(), static_cast<int> (samples.size()));
        allocations += guard.allocations();
        for (int i = 0; i < count; ++i)
        {
            const float sample = samples[static_cast<std::size_t> (i)];
            inOrder = inOrder && (i == 0 ? sample > last : static_cast<double> (sample) == static_cast<double> (last) + 1.0);
            last = sample;
        }
        read += count;
    }
    done = true;
    audio.join();

    CHECK (read >= 48000);
    CHECK (allocations == 0);
    CHECK (inOrder);
}

TEST_CASE ("The input and output taps can be read while the host prepares the Engine again at another rate or channel count")
{
    Engine engine;
    engine.prepare (48000.0, 256, 2);
    std::atomic<bool> done { false };
    std::thread reader ([&] {
        std::vector<float> samples (1 << 16);
        while (! done)
            for (const auto* tap : { &inputTap, &outputTap })
            {
                (engine.*tap->read) (samples.data(), std::min ((engine.*tap->capacity)(), static_cast<int> (samples.size())));
                (engine.*tap->skip)();
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
    CHECK (engine.inputTapSampleCapacity() >= 0.15 * 192000.0);
    CHECK (engine.outputTapSampleCapacity() >= 0.15 * 192000.0);
}
