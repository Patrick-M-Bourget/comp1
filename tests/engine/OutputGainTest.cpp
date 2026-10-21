#include "Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using namespace comp1;

TEST_CASE ("At 0 dB Output Gain the output is the input, bit for bit, at every sample rate and block size")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    const int numChannels = GENERATE (1, 2);
    const auto cut = GENERATE (from_range (test::blockCuts()));
    CAPTURE (sampleRate, numChannels, cut);

    const auto input = test::noise (numChannels, 20000);
    auto output = input;
    Engine engine;
    engine.prepare (sampleRate, 4096, numChannels);
    engine.setSettings ({ .outputGainDb = 0.0 });
    test::processInBlocks (engine, output, cut);

    CHECK (output == input);
}

TEST_CASE ("Output Gain scales every channel by its gain")
{
    const int numChannels = GENERATE (1, 2);
    const auto input = test::noise (numChannels, 4096);
    auto output = input;
    Engine engine;
    engine.prepare (48000.0, 512, numChannels);
    engine.setSettings ({ .outputGainDb = -6.0 });
    test::processInBlocks (engine, output, { 512 });

    // 10^(-6/20), worked out independently.
    constexpr double expectedGain = 0.501187233627272;
    for (std::size_t ch = 0; ch < input.size(); ++ch)
        for (std::size_t i = 0; i < input[ch].size(); ++i)
            REQUIRE (output[ch][i] == static_cast<float> (expectedGain * input[ch][i]));
}

TEST_CASE ("Output at a steady Output Gain is the same however the audio is cut into blocks")
{
    const auto cut = GENERATE (from_range (test::blockCuts()));
    CAPTURE (cut);
    const auto input = test::noise (2, 20000);
    auto whole = input, cutUp = input;
    for (auto* audio : { &whole, &cutUp })
    {
        Engine engine;
        engine.prepare (48000.0, 4096, 2);
        engine.setSettings ({ .outputGainDb = 7.5 });
        test::processInBlocks (engine, *audio, audio == &whole ? std::vector<int> { 4096 } : cut);
    }
    CHECK (cutUp == whole);
}

TEST_CASE ("The Sidechain never reaches the output")
{
    const int sidechainChannels = GENERATE (1, 2);
    const auto input = test::noise (2, 4096, 1);
    const auto sidechain = test::noise (sidechainChannels, 4096, 2);
    auto output = input;
    Engine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setSettings ({ .outputGainDb = 0.0 });
    test::processInBlocks (engine, output, { 512 }, &sidechain);
    CHECK (output == input);
}
