#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/test/AllocationGuard.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

using namespace comp1;

namespace
{

constexpr auto zeroLatency = ProcessingMode::ZeroLatency;
constexpr auto highQuality = ProcessingMode::HighQuality;

const std::vector<Oversampling>& everyOversampling()
{
    static const std::vector<Oversampling> all { Oversampling::x1, Oversampling::x2, Oversampling::x4, Oversampling::x8 };
    return all;
}

// settings at oversampling.
Settings at (Settings settings, Oversampling oversampling)
{
    settings.oversampling = oversampling;
    return settings;
}

// Compressed throughout, with Hold, Auto Gain and Output Gain all at work, in High Quality.
const Settings compressing { .thresholdDb = -30.0, .attackMs = 1.0, .holdMs = 5.0, .autoGain = true, .outputGainDb = -3.0, .processingMode = highQuality };

// Saturation on at a Drive that works it hard, at position.
Settings saturating (Settings settings, SaturationPosition position)
{
    settings.saturation = true;
    settings.driveDb = 12.0;
    settings.saturationPosition = position;
    return settings;
}

} // namespace

TEST_CASE ("An impulse comes out at exactly the reported latency at every Oversampling and sample rate")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto oversampling = GENERATE (from_range (everyOversampling()));
    CAPTURE (sampleRate, factorOf (oversampling));
    CHECK (test::impulseDelaySamples (at ({ .ratio = 1.0, .processingMode = highQuality }, oversampling), sampleRate)
           == latencySamples (highQuality, sampleRate));
}

TEST_CASE ("An Oversampling over the sample rate's cap plays the cap")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (sampleRate, static_cast<int> (position));
    auto input = test::noise (2, 6000);
    for (auto& channel : input)
        for (auto& sample : channel)
            sample *= 3.0f;
    const auto settings = saturating (compressing, position);
    const auto cap = maxOversampling (sampleRate);
    const auto atCap = test::played (at (settings, cap), sampleRate, input);
    CHECK (test::played (at (settings, Oversampling::x8), sampleRate, input) == atCap);
    // And the factors under it differ from it.
    if (cap != Oversampling::x1)
        CHECK (test::played (at (settings, static_cast<Oversampling> (static_cast<int> (cap) - 1)), sampleRate, input) != atCap);
}

// What Dry/Wet's exact null rests on (DryWetTest's nullRows() holds it at each factor): with nothing nonlinear, the Wet path is linear and aligned at every
// Oversampling (ADR 0009).
TEST_CASE ("With Saturation off, High Quality at every Oversampling is High Quality at 1x, and at Ratio 1:1 the input delayed by the reported latency, bit for bit")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto oversampling = GENERATE (from_range (everyOversampling()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, factorOf (oversampling), cut);
    const auto input = test::noise (2, 6000);
    CHECK (test::played (at (compressing, oversampling), sampleRate, input, cut)
           == test::played (at (compressing, Oversampling::x1), sampleRate, input, cut));
    CHECK (test::played (at ({ .ratio = 1.0, .processingMode = highQuality, .lookaheadMs = 2.0 }, oversampling), sampleRate, input, cut)
           == test::delayed (input, latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("High Quality at 1x and Lookahead 0 is Zero Latency's output delayed by the reported latency, bit for bit, with Saturation on and off")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    const int saturation = GENERATE (0, 1, 2); // off, Before, After
    CAPTURE (sampleRate, cut, saturation);
    auto settings = at (compressing, Oversampling::x1);
    if (saturation > 0)
        settings = saturating (settings, saturation == 1 ? SaturationPosition::Before : SaturationPosition::After);
    auto zeroLatencySettings = settings;
    zeroLatencySettings.processingMode = zeroLatency;
    // Loud enough for Saturation to work.
    auto input = test::noise (2, 6000);
    for (auto& channel : input)
        for (auto& sample : channel)
            sample *= 3.0f;
    CHECK (test::played (settings, sampleRate, input, cut)
           == test::delayed (test::played (zeroLatencySettings, sampleRate, input, cut), latencySamples (highQuality, sampleRate)));
}

TEST_CASE ("Zero Latency is the same at every Oversampling, bit for bit, at every sample rate and block size")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (sampleRate, cut, static_cast<int> (position));
    auto settings = saturating ({ .thresholdDb = -30.0, .attackMs = 1.0 }, position);
    auto input = test::noise (2, 6000);
    for (auto& channel : input)
        for (auto& sample : channel)
            sample *= 3.0f;
    const auto reference = test::played (at (settings, Oversampling::x1), sampleRate, input, cut);
    for (const auto oversampling : everyOversampling())
    {
        CAPTURE (factorOf (oversampling));
        CHECK (test::played (at (settings, oversampling), sampleRate, input, cut) == reference);
    }
    // And across a change of Oversampling.
    CHECK (test::played (at (settings, Oversampling::x1), sampleRate, input, cut, nullptr, test::SettingsChange { .at = 2003, .settings = at (settings, Oversampling::x8) })
           == reference);
}

TEST_CASE ("Oversampled, Saturation Before still changes what the compressor reacts to, as dyn-lab's steady levels say")
{
    // python3 tools/dyn-lab/dynlab.py saturation --drive 12 --level 12, as SaturationTest checks it at the base rate:
    // Before -14.99 dB, After -10.52 dB.
    const double sampleRate = GENERATE (44100.0, 96000.0);
    const auto oversampling = GENERATE (from_range (everyOversampling()));
    CAPTURE (sampleRate, factorOf (oversampling));
    const auto steadyDb = [&] (SaturationPosition position) {
        const Settings settings { .saturation = true, .driveDb = 12.0, .saturationPosition = position, .processingMode = highQuality, .oversampling = oversampling };
        return test::steadyOutputDb (settings, 12.0, sampleRate, 2, 0.5, test::constantLevel);
    };
    CHECK (std::abs (steadyDb (SaturationPosition::Before) - -14.99) <= 0.01);
    CHECK (std::abs (steadyDb (SaturationPosition::After) - -10.52) <= 0.01);
}

TEST_CASE ("In High Quality, External detection keyed from the input meets the audio as Internal detection does, at every Oversampling")
{
    // A Lookahead step, keyed from the Sidechain carrying the input itself, comes out sample for sample as keyed
    // internally: the Sidechain is delayed with the audio, so External detection keeps Internal's timing.
    const double sampleRate = GENERATE (44100.0, 192000.0);
    const auto oversampling = GENERATE (from_range (everyOversampling()));
    // A sample at a time, and cuts across runs: few, to keep the test short.
    const auto cut = GENERATE (std::vector<int> { 512 }, std::vector<int> { 1 }, std::vector<int> { 33, 1, 15, 128 });
    CAPTURE (sampleRate, factorOf (oversampling), cut);
    const int length = static_cast<int> (0.05 * sampleRate);
    auto input = test::steadyLevel (2, length, -40.0);
    const auto loud = test::steadyLevel (2, length, 0.0);
    for (std::size_t ch = 0; ch < input.size(); ++ch)
        input[ch].insert (input[ch].end(), loud[ch].begin(), loud[ch].end());
    const Settings internal { .thresholdDb = -20.0, .attackMs = 0.5, .releaseMs = 20.0, .processingMode = highQuality, .lookaheadMs = 2.0, .oversampling = oversampling };
    auto external = internal;
    external.detectionSource = DetectionSource::External;
    CHECK (test::played (external, sampleRate, input, cut, &input) == test::played (internal, sampleRate, input, cut));
}

TEST_CASE ("With Saturation off, an Oversampling change leaves the output as it is, bit for bit")
{
    const double sampleRate = GENERATE (44100.0, 96000.0, 192000.0);
    const auto& cut = GENERATE_REF (from_range (test::blockCuts()));
    CAPTURE (sampleRate, cut);
    const auto input = test::noise (2, 8000);
    const auto reference = test::played (at (compressing, Oversampling::x1), sampleRate, input, cut);
    for (const auto [from, to] : { std::pair { Oversampling::x1, Oversampling::x8 }, std::pair { Oversampling::x8, Oversampling::x1 } })
    {
        CAPTURE (factorOf (from), factorOf (to));
        CHECK (test::played (at (compressing, from), sampleRate, input, cut, nullptr, test::SettingsChange { .at = 3001, .settings = at (compressing, to) })
               == reference);
    }
}

TEST_CASE ("An Oversampling change gives the same output at every block size")
{
    const auto [from, to] = GENERATE (table<Oversampling, Oversampling> ({ { Oversampling::x1, Oversampling::x8 }, { Oversampling::x8, Oversampling::x1 } }));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (factorOf (from), factorOf (to), static_cast<int> (position));
    auto input = test::noise (2, 8000);
    for (auto& channel : input)
        for (auto& sample : channel)
            sample *= 3.0f;
    // Saturation at Drive 24 dB, so After the compressor it saturates too.
    auto settings = saturating (compressing, position);
    settings.driveDb = 24.0;
    const test::SettingsChange change { .at = 1000, .settings = at (settings, to) };
    const auto reference = test::played (at (settings, from), 48000.0, input, { 4096 }, nullptr, change);
    // The change is heard: the output isn't what staying would give.
    CHECK (reference != test::played (at (settings, from), 48000.0, input, { 4096 }));
    for (const auto& cut : test::blockCuts())
    {
        CAPTURE (cut);
        CHECK (test::played (at (settings, from), 48000.0, input, cut, nullptr, change) == reference);
    }
}

TEST_CASE ("A non-finite input sample never reaches the output at any Oversampling, Saturation on")
{
    const auto oversampling = GENERATE (from_range (everyOversampling()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (factorOf (oversampling), static_cast<int> (position));
    auto broken = test::noise (2, 8192), silenced = broken;
    for (const std::size_t i : { 100u, 2000u, 3001u, 5000u })
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            broken[ch][i] = i % 2 == 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            silenced[ch][i] = 0.0f;
        }
    const auto settings = at (saturating (compressing, position), oversampling);
    const auto out = test::played (settings, 48000.0, broken, { 512 });
    CHECK (out == test::played (settings, 48000.0, silenced, { 512 }));
    for (const auto& channel : out)
        for (const float sample : channel)
            REQUIRE (std::isfinite (sample));
}

TEST_CASE ("The Engine does not allocate at any Oversampling, nor across a change of it")
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (44100.0, blockSize, 2);
    auto audio = test::noise (2, blockSize);
    std::vector<float*> main { audio[0].data(), audio[1].data() };

    plugkit::test::AllocationGuard guard;
    for (int block = 0; block < 64; ++block)
    {
        // Oversampling moving every few blocks, some of the time mid-fade, with Saturation on and moving, and the
        // Processing Mode switching now and then; blocks cutting runs.
        engine.setSettings ({ .saturation = block % 7 != 3,
                              .driveDb = 18.0,
                              .saturationPosition = (block / 5) % 2 == 0 ? SaturationPosition::Before : SaturationPosition::After,
                              .processingMode = block % 11 == 10 ? zeroLatency : highQuality,
                              .oversampling = static_cast<Oversampling> ((block / 3) % 4) });
        const int count = block % 3 == 0 ? blockSize : 1 + (block * 37) % blockSize;
        engine.process ({ main.data(), 2, count });
    }
    REQUIRE (guard.allocations() == 0);
}
