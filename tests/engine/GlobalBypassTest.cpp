#include "Measure.h"

#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/test/AllocationGuard.h>

#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;

// A setting Global Bypass is checked under, each far from the default, so the processed output is far from the input.
struct Scene
{
    std::string name;
    std::function<void (Settings&)> set;
};

const std::vector<Scene>& scenes()
{
    static const std::vector<Scene> all {
        { "deep compression", [] (Settings& s) {
             s.thresholdDb = -50.0;
             s.ratio = parameterRanges::inf;
             s.attackMs = 0.005;
         } },
        { "Auto Gain on", [] (Settings& s) { s.autoGain = true; } },
        { "Output Gain +12 dB", [] (Settings& s) { s.outputGainDb = 12.0; } },
        { "Lookahead 5 ms", [] (Settings& s) { s.lookaheadMs = 5.0; } },
        { "Saturation at Drive 24 dB", [] (Settings& s) {
             s.saturation = true;
             s.driveDb = 24.0;
         } },
    };
    return all;
}

// Settings compressing noise throughout, in mode, with Global Bypass as given.
Settings compressing (ProcessingMode mode, bool bypassed)
{
    return { .thresholdDb = -30.0, .ratio = 8.0, .processingMode = mode, .globalBypass = bypassed };
}

} // namespace

TEST_CASE ("Global Bypass settled on plays the input delayed by the latency, bit for bit, at every rate, block size and setting", "[sweep]")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const auto& scene = GENERATE (from_range (scenes()));
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (static_cast<int> (mode), scene.name, sampleRate);
    auto settings = compressing (mode, false);
    scene.set (settings);
    const auto input = test::noise (2, 6000);
    REQUIRE (test::played (settings, sampleRate, input) != test::delayed (input, latencySamples (mode, sampleRate)));
    settings.globalBypass = true;
    for (const auto& cut : test::blockCuts())
    {
        CAPTURE (cut);
        CHECK (test::played (settings, sampleRate, input, cut) == test::delayed (input, latencySamples (mode, sampleRate)));
    }
}

TEST_CASE ("The Engine processes on under Global Bypass, so turning it off fades to the compressor's current Gain Reduction")
{
    // One Engine is bypassed for the first half second, the other never is. Once the crossfade back has settled,
    // the two play the same, bit for bit, in each Processing Mode.
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    constexpr int switchAt = 24000;
    const auto input = test::noise (2, 48000, 11);
    const auto neverBypassed = test::played (compressing (mode, false), 48000.0, input);
    const auto bypassed = test::played (compressing (mode, true), 48000.0, input, { 512 }, nullptr, test::SettingsChange { switchAt, compressing (mode, false) });
    REQUIRE (bypassed != neverBypassed);
    // 200 ms on: the crossfade back has settled.
    for (std::size_t ch = 0; ch < input.size(); ++ch)
        CHECK (std::vector<float> (bypassed[ch].begin() + switchAt + 9600, bypassed[ch].end())
               == std::vector<float> (neverBypassed[ch].begin() + switchAt + 9600, neverBypassed[ch].end()));
}

TEST_CASE ("A Processing Mode switch under Global Bypass falls to silence and rises again, then plays the input at the new path's latency")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto [from, to] = GENERATE (table<ProcessingMode, ProcessingMode> ({ { zeroLatency, highQuality }, { highQuality, zeroLatency } }));
    CAPTURE (sampleRate, static_cast<int> (to));
    // dyn-lab's MODE_FADE_MS: the fade-out, and the fade-in, each 10 ms. Into High Quality, the silence also waits
    // for its delays to fill: all of them, its whole latency, as ProcessingModeTest's switch says.
    const int fade = static_cast<int> (std::lround (10.0 * sampleRate / 1000.0));
    const int padding = to == highQuality ? latencySamples (highQuality, sampleRate) : 0;
    const int switchAt = 4003;
    const auto input = test::noise (2, switchAt + 2 * fade + padding + 3000);
    const auto reference = test::played (compressing (from, true), sampleRate, input, { 4096 }, nullptr, test::SettingsChange { switchAt, compressing (to, true) });

    const auto oldPath = test::delayed (input, latencySamples (from, sampleRate));
    const auto newPath = test::delayed (input, latencySamples (to, sampleRate));
    const auto at = [] (int i) { return static_cast<std::size_t> (i); };
    for (std::size_t ch = 0; ch < input.size(); ++ch)
    {
        CAPTURE (ch);
        const auto& out = reference[ch];
        for (int i = 0; i < switchAt; ++i)
            REQUIRE (out[at (i)] == oldPath[ch][at (i)]);
        for (int i = switchAt; i < switchAt + fade; ++i)
            REQUIRE (std::abs (out[at (i)]) <= std::abs (oldPath[ch][at (i)]));
        for (int i = switchAt + fade - 1; i < switchAt + fade + padding; ++i)
            REQUIRE (out[at (i)] == 0.0f);
        // Fading in from the input itself, which the reference's delay holds in both Processing Modes, never from
        // silence the delay was left holding.
        for (int i = switchAt + fade + padding; i < switchAt + 2 * fade + padding; ++i)
        {
            REQUIRE (std::abs (out[at (i)]) <= std::abs (newPath[ch][at (i)]));
            REQUIRE ((out[at (i)] != 0.0f || newPath[ch][at (i)] == 0.0f));
        }
        for (auto i = at (switchAt + 2 * fade + padding); i < out.size(); ++i)
            REQUIRE (out[i] == newPath[ch][i]);
    }
    for (const auto& cut : test::blockCuts())
    {
        CAPTURE (cut);
        CHECK (test::played (compressing (from, true), sampleRate, input, cut, nullptr, test::SettingsChange { switchAt, compressing (to, true) }) == reference);
    }
}

TEST_CASE ("Global Bypass takes precedence over Detection Audition: it plays the delayed input, not the detection signal")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (static_cast<int> (mode));
    // External, so the detection signal Detection Audition plays is the Sidechain, not the main input.
    auto settings = compressing (mode, true);
    settings.detectionSource = DetectionSource::External;
    settings.detectionAudition = true;
    const auto input = test::noise (2, 8192);
    const auto sidechainSamples = test::noise (2, 8192, 5);
    CHECK (test::played (settings, 48000.0, input, { 512 }, &sidechainSamples) == test::delayed (input, latencySamples (mode, 48000.0)));
}

TEST_CASE ("A non-finite input sample never reaches the output under Global Bypass, nor across a switch")
{
    const auto mode = GENERATE (zeroLatency, highQuality);
    const bool laterOn = GENERATE (false, true);
    CAPTURE (static_cast<int> (mode), laterOn);
    const auto input = test::noise (2, 8192);
    auto broken = input, silenced = input;
    for (const std::size_t i : { 100u, 2000u, 2600u, 5000u })
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            broken[ch][i] = i % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][i] = 0.0f;
        }
    const test::SettingsChange change { 2500, compressing (mode, laterOn) };
    CHECK (test::played (compressing (mode, true), 48000.0, broken, { 512 }, nullptr, change)
           == test::played (compressing (mode, true), 48000.0, silenced, { 512 }, nullptr, change));
}

TEST_CASE ("The Engine does not allocate across a Global Bypass switch, in either Processing Mode")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (48000.0, blockSize, 2);
    auto audio = test::noise (2, blockSize);
    std::vector<float*> main { audio[0].data(), audio[1].data() };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // Global Bypass switching every other block, some of the time mid-crossfade, the mode every few, and blocks
        // cutting runs.
        engine.setSettings (compressing ((block / 5) % 2 == 0 ? zeroLatency : highQuality, (block / 2) % 2 == 0));
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main.data(), 2, count });
    }
    REQUIRE (guard.allocations() == 0);
}
