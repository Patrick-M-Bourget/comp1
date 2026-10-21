#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cstddef>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;

// audio delayed by samples, the start filled with silence and the end cut off, so it keeps its length.
test::Channels delayed (const test::Channels& audio, int samples)
{
    auto out = audio;
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
        for (std::size_t i = 0; i < audio[ch].size(); ++i)
            out[ch][i] = i < static_cast<std::size_t> (samples) ? 0.0f : audio[ch][i - static_cast<std::size_t> (samples)];
    return out;
}

// audio through a fresh Engine at sampleRate with settings, cut as given; then, from sample switchAt on, with later.
test::Channels play (const test::Channels& audio, double sampleRate, const std::vector<int>& cut, const Settings& settings,
                     const Settings& later, int switchAt)
{
    auto first = audio, second = audio;
    for (auto& channel : first)
        channel.resize (static_cast<std::size_t> (switchAt));
    for (auto& channel : second)
        channel.erase (channel.begin(), channel.begin() + switchAt);
    Engine engine;
    engine.prepare (sampleRate, 4096, static_cast<int> (audio.size()));
    engine.setSettings (settings);
    test::processInBlocks (engine, first, cut);
    engine.setSettings (later);
    test::processInBlocks (engine, second, cut);
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
        first[ch].insert (first[ch].end(), second[ch].begin(), second[ch].end());
    return first;
}

test::Channels play (const test::Channels& audio, double sampleRate, const std::vector<int>& cut, const Settings& settings)
{
    return play (audio, sampleRate, cut, settings, settings, 0);
}

} // namespace

TEST_CASE ("An impulse comes out delayed by the latency each Processing Mode reports, at every sample rate")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (static_cast<int> (mode), sampleRate);
    const Settings settings { .ratio = 1.0, .processingMode = mode };
    CHECK (test::impulseDelaySamples (settings, sampleRate) == latencySamples (mode, sampleRate));
}

TEST_CASE ("High Quality at Ratio 1:1 is the input delayed by the reported latency, bit for bit, at any Lookahead")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    const double lookaheadMs = GENERATE (0.0, 2.5, 5.0);
    CAPTURE (sampleRate, cut, lookaheadMs);
    const auto input = test::noise (2, 6000);
    const Settings settings { .ratio = 1.0, .processingMode = highQuality, .lookaheadMs = lookaheadMs };
    CHECK (play (input, sampleRate, cut, settings) == delayed (input, latencySamples (highQuality, sampleRate)));

    // Lookahead jumping to the other end of its range, part-way through a run, moves nothing the listener hears.
    auto jumped = settings;
    jumped.lookaheadMs = lookaheadMs < 2.5 ? 5.0 : 0.0;
    CHECK (play (input, sampleRate, cut, settings, jumped, 2003) == delayed (input, latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("High Quality at Lookahead 0 is Zero Latency's output delayed by the reported latency, bit for bit")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    // Compressed throughout, with Hold, Auto Gain and Output Gain all at work.
    const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0, .holdMs = 5.0, .autoGain = true, .outputGainDb = -3.0 };
    auto highQualitySettings = compressing;
    highQualitySettings.processingMode = highQuality;
    const auto input = test::noise (2, 6000);
    const auto zeroLatencyOutput = play (input, sampleRate, cut, compressing);
    CHECK (play (input, sampleRate, cut, highQualitySettings) == delayed (zeroLatencyOutput, latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("In Zero Latency, Lookahead changes nothing, at every sample rate and block size")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0 };
    auto looking = compressing;
    looking.lookaheadMs = 5.0;
    const auto input = test::noise (2, 6000);
    const auto without = play (input, sampleRate, cut, compressing);
    CHECK (play (input, sampleRate, cut, looking) == without);
    CHECK (play (input, sampleRate, cut, compressing, looking, 2003) == without);
}
