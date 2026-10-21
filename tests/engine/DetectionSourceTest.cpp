#include "Measure.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <vector>

using namespace comp1;

namespace
{

// Settings at which the detector holds the peak of a steady level: Attack at its fastest, Release at its slowest and
// Hold at its longest. At Threshold -18, Ratio ∞:1 and Knee 0 the Transfer Curve takes 18 dB off at 0 dB and 12 dB at
// -6 dB (python3 tools/dyn-lab/dynlab.py transfer --threshold -18 --ratio inf --knee 0).
Settings external()
{
    return { .thresholdDb = -18.0,
             .ratio = parameterRanges::inf,
             .kneeDb = 0.0,
             .attackMs = 0.005,
             .releaseMs = 5000.0,
             .holdMs = 500.0,
             .detectionSource = DetectionSource::External };
}

// The main input: a steady -30 dB, below Threshold, so only the Sidechain can move it.
constexpr double mainDb = -30.0;

// The gain in dB each main channel's last sample got, after seconds of a steady main input with sidechainSamples on
// the Sidechain (none when null).
std::vector<double> gainsDb (const Settings& settings, double sampleRate, int mainChannels, const test::Channels* sidechainSamples, double seconds = 0.5)
{
    const int numSamples = static_cast<int> (seconds * sampleRate);
    const auto input = test::steadyLevel (mainChannels, numSamples, mainDb);
    auto output = input;
    Engine engine;
    engine.prepare (sampleRate, 512, mainChannels);
    engine.setSettings (settings);
    test::processInBlocks (engine, output, { 512 }, sidechainSamples);
    std::vector<double> gains;
    for (std::size_t ch = 0; ch < output.size(); ++ch)
        gains.push_back (20.0 * std::log10 (static_cast<double> (output[ch].back()) / static_cast<double> (input[ch].back())));
    return gains;
}

// A steady Sidechain, its channels at the levels given in dB.
test::Channels steadySidechain (const std::vector<double>& levelsDb, double sampleRate, double seconds = 0.5)
{
    test::Channels channels;
    for (const double db : levelsDb)
        channels.push_back (test::steadyLevel (1, static_cast<int> (seconds * sampleRate), db).front());
    return channels;
}

} // namespace

TEST_CASE ("With External and a stereo Sidechain, each main channel is compressed by the Sidechain channel of its own index")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    // Full scale on the left, -6 dB on the right: the two take different Gain Reductions.
    const auto sidechainSamples = steadySidechain ({ 0.0, -6.0 }, sampleRate);
    const auto gains = gainsDb (external(), sampleRate, 2, &sidechainSamples);
    CHECK (gains[0] == Catch::Approx (-18.0).margin (0.1));
    CHECK (gains[1] == Catch::Approx (-12.0).margin (0.1));
}

TEST_CASE ("With External and a mono Sidechain, every main channel is compressed by it")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    const auto sidechainSamples = steadySidechain ({ 0.0 }, sampleRate);
    const auto gains = gainsDb (external(), sampleRate, 2, &sidechainSamples);
    CHECK (gains[0] == Catch::Approx (-18.0).margin (0.1));
    CHECK (gains[1] == Catch::Approx (-18.0).margin (0.1));
}

TEST_CASE ("With External, a stereo Sidechain and a mono main, the main is compressed by the louder Sidechain channel")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    // The louder channel on either side: following one side alone, or the two's average, takes off less.
    const auto levelsDb = GENERATE (std::vector<double> { 0.0, -6.0 }, std::vector<double> { -6.0, 0.0 });
    CAPTURE (sampleRate, levelsDb);
    const auto sidechainSamples = steadySidechain (levelsDb, sampleRate);
    CHECK (gainsDb (external(), sampleRate, 1, &sidechainSamples)[0] == Catch::Approx (-18.0).margin (0.1));
}
