#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/test/AllocationGuard.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;

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
    CHECK (play (input, sampleRate, cut, settings) == test::delayed (input, latencySamples (highQuality, sampleRate)));

    // Lookahead jumping to the other end of its range, part-way through a run, moves nothing the listener hears.
    auto jumped = settings;
    jumped.lookaheadMs = lookaheadMs < 2.5 ? 5.0 : 0.0;
    CHECK (play (input, sampleRate, cut, settings, jumped, 2003) == test::delayed (input, latencySamples (highQuality, sampleRate)));
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
    CHECK (play (input, sampleRate, cut, highQualitySettings) == test::delayed (zeroLatencyOutput, latencySamples (highQuality, sampleRate)));
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

namespace
{
// dyn-lab's MODE_FADE_MS (python3 tools/dyn-lab/dynlab.py latency): a switch's fade-out, and its fade-in, each this long.
constexpr double modeFadeMs = 10.0;
} // namespace

TEST_CASE ("A Processing Mode switch falls to silence and rises again within the mode fade each way, the same at every block size")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto [from, to] = GENERATE (table<ProcessingMode, ProcessingMode> ({ { zeroLatency, highQuality }, { highQuality, zeroLatency } }));
    CAPTURE (sampleRate, static_cast<int> (to));
    const int fade = static_cast<int> (std::lround (modeFadeMs * sampleRate / 1000.0));
    // Into High Quality, the silence lasts while its delays after the compressor, the reserves' padding, fill with the
    // new path's audio, so the fade-in starts from it: High Quality's latency less the Lookahead budget.
    const int padding = to == highQuality ? latencySamples (highQuality, sampleRate) - lookaheadBudgetSamples (sampleRate) : 0;
    // Part-way through a run, once the old path has played long enough to fill its delays.
    const int switchAt = 4003;
    const auto input = test::noise (2, switchAt + 2 * fade + padding + 3000);
    const Settings before { .ratio = 1.0, .processingMode = from }, after { .ratio = 1.0, .processingMode = to };
    const auto reference = play (input, sampleRate, { 4096 }, before, after, switchAt);

    const auto oldPath = test::delayed (input, latencySamples (from, sampleRate));
    const auto newPath = test::delayed (input, latencySamples (to, sampleRate));
    const auto at = [] (int i) { return static_cast<std::size_t> (i); };
    for (std::size_t ch = 0; ch < input.size(); ++ch)
    {
        CAPTURE (ch);
        const auto& out = reference[ch];
        for (int i = 0; i < switchAt; ++i)
            REQUIRE (out[at (i)] == oldPath[ch][at (i)]);
        // Fading out, never louder than the old path.
        for (int i = switchAt; i < switchAt + fade; ++i)
            REQUIRE (std::abs (out[at (i)]) <= std::abs (oldPath[ch][at (i)]));
        // Silent by the fade-out's last sample, and until the fade-in starts.
        for (int i = switchAt + fade - 1; i < switchAt + fade + padding; ++i)
            REQUIRE (out[at (i)] == 0.0f);
        // Fading in, never louder than the new path, which then plays as it is.
        for (int i = switchAt + fade + padding; i < switchAt + 2 * fade + padding; ++i)
            REQUIRE (std::abs (out[at (i)]) <= std::abs (newPath[ch][at (i)]));
        for (auto i = at (switchAt + 2 * fade + padding); i < out.size(); ++i)
            REQUIRE (out[i] == newPath[ch][i]);
    }

    for (const auto& cut : test::blockCuts())
    {
        CAPTURE (cut);
        CHECK (play (input, sampleRate, cut, before, after, switchAt) == reference);
    }
}

TEST_CASE ("The settings in force at prepare apply at once, with no mode fade")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    const auto input = test::noise (2, 3000);
    CHECK (play (input, 48000.0, { 512 }, { .ratio = 1.0, .processingMode = mode }) == test::delayed (input, latencySamples (mode, 48000.0)));
}

TEST_CASE ("A non-finite input sample never reaches the output in High Quality, nor across a switch")
{
    const auto later = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (later));
    const auto input = test::noise (2, 8192);
    auto broken = input, silenced = input;
    for (const std::size_t i : { 100u, 2000u, 3001u, 5000u })
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            broken[ch][i] = i % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][i] = 0.0f;
        }
    const Settings highQualitySettings { .autoGain = true, .processingMode = highQuality, .lookaheadMs = 3.0 };
    auto laterSettings = highQualitySettings;
    laterSettings.processingMode = later;
    CHECK (play (broken, 48000.0, { 512 }, highQualitySettings, laterSettings, 2500)
           == play (silenced, 48000.0, { 512 }, highQualitySettings, laterSettings, 2500));
}

TEST_CASE ("The Engine does not allocate in either Processing Mode, nor across a switch")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (48000.0, blockSize, 2);
    auto audio = test::noise (2, blockSize);
    std::vector<float*> main { audio[0].data(), audio[1].data() };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // The mode switching every few blocks, some of the time mid-fade, Lookahead moving, and blocks cutting runs.
        engine.setSettings ({ .thresholdDb = -30.0,
                              .processingMode = (block / 3) % 2 == 0 ? zeroLatency : highQuality,
                              .lookaheadMs = 0.5 * (block % 11) });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main.data(), 2, count });
    }
    REQUIRE (guard.allocations() == 0);
}
