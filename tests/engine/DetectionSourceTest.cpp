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

// The gain in dB each main channel's last sample got (its magnitude, so a delay of an odd number of samples in High
// Quality, which flips the steady level's sign, reads the same), after seconds of a steady main input with
// sidechainSamples on the Sidechain (none when null).
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
        gains.push_back (20.0 * std::log10 (std::abs (static_cast<double> (output[ch].back()) / static_cast<double> (input[ch].back()))));
    return gains;
}

// A steady Sidechain, its channels at the levels given in dB.
test::Channels steadySidechain (const std::vector<double>& levelsDb, double sampleRate, double seconds = 0.5)
{
    return test::steadyLevels (levelsDb, static_cast<int> (seconds * sampleRate));
}

} // namespace

TEST_CASE ("With External and a stereo Sidechain, each main channel is compressed by the Sidechain channel of its own index")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    // Full scale on the left, -6 dB on the right: the two take different Gain Reductions.
    const auto sidechainSamples = steadySidechain ({ 0.0, -6.0 }, sampleRate);
    // Unlinked, so each channel takes its own.
    auto settings = external();
    settings.stereoLink = 0.0;
    const auto gains = gainsDb (settings, sampleRate, 2, &sidechainSamples);
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

TEST_CASE ("With Internal, a loud Sidechain changes nothing in the output, bit for bit")
{
    const int sidechainChannels = GENERATE (1, 2);
    CAPTURE (sidechainChannels);
    const auto input = test::noise (2, 8192);
    const auto sidechainSamples = test::steadyLevel (sidechainChannels, 8192, 0.0);
    auto alone = input, keyed = input;
    for (auto* audio : { &alone, &keyed })
    {
        Engine engine;
        engine.prepare (48000.0, 512, 2);
        engine.setSettings ({ .thresholdDb = -24.0 });
        test::processInBlocks (engine, *audio, { 512 }, audio == &keyed ? &sidechainSamples : nullptr);
    }
    REQUIRE (alone != input); // compressed
    CHECK (keyed == alone);
}

TEST_CASE ("With External and no Sidechain connected, a loud main input passes bit for bit")
{
    // A Sidechain that is null, has no channels, or has fewer samples than the block counts as none connected.
    enum Missing { null, noChannels, tooShort };
    const auto missing = GENERATE (null, noChannels, tooShort);
    CAPTURE (missing);
    constexpr int blockSize = 512;
    const auto input = test::noise (2, 16 * blockSize); // peaks near -6 dB, 12 dB over Threshold
    auto output = input;
    const std::vector<float> sidechainSamples (blockSize / 2, 1.0f);
    const float* sidechainChannels[] = { sidechainSamples.data() };
    const ConstAudioBlock sidechain { sidechainChannels, missing == noChannels ? 0 : 1, blockSize / 2 };

    Engine engine;
    engine.prepare (48000.0, blockSize, 2);
    engine.setSettings (external());
    for (int start = 0; start < static_cast<int> (output[0].size()); start += blockSize)
    {
        float* main[] = { output[0].data() + start, output[1].data() + start };
        engine.process ({ main, 2, blockSize }, missing == null ? nullptr : &sidechain);
    }
    CHECK (output == input);
}

TEST_CASE ("With External, Gain Reduction returns at Release once the Sidechain is gone, and comes back with it")
{
    constexpr double sampleRate = 48000.0;
    auto settings = external();
    settings.releaseMs = 50.0;
    settings.holdMs = 0.0;
    Engine engine;
    engine.prepare (sampleRate, 512, 2);
    engine.setSettings (settings);

    // The gain in dB the last sample of a stretch of the steady main input got, with sidechainSamples or none.
    const auto play = [&engine] (int numSamples, const test::Channels* sidechainSamples) {
        const auto input = test::steadyLevel (2, numSamples, mainDb);
        auto output = input;
        test::processInBlocks (engine, output, { 512 }, sidechainSamples);
        return 20.0 * std::log10 (static_cast<double> (output[0].back()) / static_cast<double> (input[0].back()));
    };
    const auto sidechainSamples = steadySidechain ({ 0.0 }, sampleRate, 0.2);
    const auto sidechainLength = static_cast<int> (sidechainSamples[0].size());
    CHECK (play (sidechainLength, &sidechainSamples) == Catch::Approx (-18.0).margin (0.1));
    // Release gives back 1 - 1/e of the Gain Reduction in Release's time, one pole, as dyn-lab's step measures it.
    const auto releaseSamples = static_cast<int> (settings.releaseMs * sampleRate / 1000.0);
    CHECK (play (releaseSamples, nullptr) == Catch::Approx (-18.0 / std::exp (1.0)).margin (0.1));
    CHECK (play (20 * releaseSamples, nullptr) == Catch::Approx (0.0).margin (0.01));
    CHECK (play (sidechainLength, &sidechainSamples) == Catch::Approx (-18.0).margin (0.1));
}

TEST_CASE ("With External, the output is the same however the audio is cut into blocks")
{
    const auto& cut = GENERATE (from_range (test::blockCuts()));
    const int sidechainChannels = GENERATE (1, 2);
    CAPTURE (cut, sidechainChannels);
    const auto input = test::noise (2, 20000);
    const auto sidechainSamples = test::noise (sidechainChannels, 20000, 4);
    auto whole = input, cutUp = input;
    for (auto* audio : { &whole, &cutUp })
    {
        Engine engine;
        engine.prepare (48000.0, 20000, 2);
        engine.setSettings ({ .thresholdDb = -30.0, .ratio = 8.0, .attackMs = 1.0, .detectionSource = DetectionSource::External });
        test::processInBlocks (engine, *audio, audio == &whole ? std::vector<int> { 20000 } : cut, &sidechainSamples);
    }
    REQUIRE (whole != input); // compressed
    CHECK (cutUp == whole);
}

TEST_CASE ("With External, a stereo Sidechain and a mono main, the main is compressed by the louder Sidechain channel")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    // The louder channel on either side: following one side alone, or the two's average, takes off less.
    const auto levelsDb = GENERATE (std::vector<double> { 0.0, -6.0 }, std::vector<double> { -6.0, 0.0 });
    // In High Quality too, whose Lookahead delays what the main follows rather than the Sidechain's two channels.
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (sampleRate, levelsDb, mode);
    auto settings = external();
    settings.processingMode = mode;
    settings.lookaheadMs = 2.0;
    const auto sidechainSamples = steadySidechain (levelsDb, sampleRate);
    CHECK (gainsDb (settings, sampleRate, 1, &sidechainSamples)[0] == Catch::Approx (-18.0).margin (0.1));
}
