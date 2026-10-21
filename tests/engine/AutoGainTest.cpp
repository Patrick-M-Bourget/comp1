#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

struct Estimate
{
    std::string command; // the dyn-lab command that printed autoGainDb
    Settings settings;
    double autoGainDb;
};

constexpr double infinity = std::numeric_limits<double>::infinity();

// The settings tools/dyn-lab/USAGE.md lists for the Auto Gain check, each with the estimate its command prints, with
// Auto Gain on. Each command with --character vca or fet prints the same estimate, the Transfer Curve's, and checks it
// on that Character.
const std::vector<Estimate>& estimates()
{
    static const std::vector<Estimate> all {
        { "python3 tools/dyn-lab/dynlab.py autogain", { .autoGain = true }, 3.2470 },
        { "python3 tools/dyn-lab/dynlab.py autogain --ratio inf", { .ratio = infinity, .autoGain = true }, 4.3294 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 0", { .kneeDb = 0.0, .autoGain = true }, 3.2143 },
        { "python3 tools/dyn-lab/dynlab.py autogain --knee 72", { .kneeDb = 72.0, .autoGain = true }, 6.7847 },
        { "python3 tools/dyn-lab/dynlab.py autogain --range 6", { .rangeDb = 6.0, .autoGain = true }, 2.1756 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30", { .thresholdDb = -30.0, .autoGain = true }, 8.6042 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -30 --ratio inf",
          { .thresholdDb = -30.0, .ratio = infinity, .autoGain = true },
          11.4722 },
        { "python3 tools/dyn-lab/dynlab.py autogain --threshold -6 --ratio 2",
          { .thresholdDb = -6.0, .ratio = 2.0, .autoGain = true },
          0.3075 },
    };
    return all;
}

// dyn-lab's reference signal (tools/dyn-lab/USAGE.md, "autogain"), written again from its description: pink-like noise
// from a 32-bit linear congruential generator (seed 1) through Paul Kellet's three-pole filter, 0.5 s at 48 kHz at
// each Detection Level from -40 to 0 dB in 2 dB steps, lowest first, each segment at the power of a sine whose peak
// reads its level.
constexpr double referenceRate = 48000.0;
constexpr double autoGainToleranceDb = 1.0; // dyn-lab's AUTO_GAIN_TOLERANCE

std::vector<std::vector<float>> referenceSegments()
{
    std::uint32_t state = 1;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;
    std::vector<std::vector<float>> segments;
    for (int levelDb = -40; levelDb <= 0; levelDb += 2)
    {
        std::vector<double> noise (static_cast<std::size_t> (0.5 * referenceRate));
        double power = 0.0;
        for (auto& x : noise)
        {
            state = 1664525u * state + 1013904223u;
            const double w = state / 2147483648.0 - 1.0;
            b0 = 0.99765 * b0 + 0.0990460 * w;
            b1 = 0.96300 * b1 + 0.2965164 * w;
            b2 = 0.57000 * b2 + 1.0526913 * w;
            x = b0 + b1 + b2 + 0.1848 * w;
            power += x * x;
        }
        const double scale = std::sqrt (std::pow (10.0, levelDb / 10.0) / 2.0 / (power / static_cast<double> (noise.size())));
        auto& segment = segments.emplace_back();
        for (const double x : noise)
            segment.push_back (static_cast<float> (x * scale));
    }
    return segments;
}

} // namespace

TEST_CASE ("Auto Gain adds dyn-lab's estimate within 0.01 dB, worked out from the settings, on every Character")
{
    const auto& estimate = GENERATE (from_range (estimates()));
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    CAPTURE (estimate.command, static_cast<int> (character));
    auto settings = estimate.settings;
    settings.character = character;
    // At -120 dB nothing is compressed (the lowest Knee here starts at -54 dB), so the output level is Auto Gain's.
    const double gainDb = test::steadyOutputDb (settings, -120.0, 48000.0) + 120.0;
    CHECK (std::abs (gainDb - estimate.autoGainDb) <= 0.01);
}

TEST_CASE ("With Auto Gain on, the reference signal keeps its level within dyn-lab's tolerance, on every Character", "[sweep]")
{
    const auto& estimate = GENERATE (from_range (estimates()));
    const auto character = GENERATE (Character::Clean, Character::VCA, Character::FET, Character::Opto);
    CAPTURE (estimate.command, static_cast<int> (character));
    auto settings = estimate.settings;
    settings.character = character;
    // The lab's check runs without Colour, which Auto Gain doesn't compensate.
    settings.colour = 0.0;
    const auto segments = referenceSegments();
    Engine engine;
    engine.prepare (referenceRate, 512, 1);
    engine.setSettings (settings);
    // Each segment's level difference, output minus input, averaged in dB over the segments, as dyn-lab measures it.
    double sumDb = 0.0;
    for (const auto& segment : segments)
    {
        test::Channels audio { segment };
        test::processInBlocks (engine, audio, { 512 });
        double powerIn = 0.0, powerOut = 0.0;
        for (std::size_t i = 0; i < segment.size(); ++i)
        {
            powerIn += static_cast<double> (segment[i]) * segment[i];
            powerOut += static_cast<double> (audio.front()[i]) * audio.front()[i];
        }
        sumDb += 10.0 * std::log10 (powerOut / powerIn);
    }
    const double differenceDb = sumDb / static_cast<double> (segments.size());
    CAPTURE (differenceDb);
    CHECK (std::abs (differenceDb) <= autoGainToleranceDb);
}
