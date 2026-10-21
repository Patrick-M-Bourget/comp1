#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

using namespace comp1;

namespace
{
// Output Gain's glide, from the spec of the Smoother it uses: two one-pole stages, critically damped, with a time
// constant of 7 ms. Its step response, 1 - (1 + t/T) e^(-t/T), rises fastest at t = T, by 1/(e T) of the jump per
// sample: no sample's step is larger than that, with 1% for the steps being discrete.
constexpr double glideTimeConstantSeconds = 0.007;
double largestGlideStep (double jump, double sampleRate)
{
    return 1.01 * std::abs (jump) / (std::exp (1.0) * glideTimeConstantSeconds * sampleRate);
}

// The block size the cut-up output is compared against.
const std::vector<int> wholeCut { 4096 };
} // namespace

TEST_CASE ("At 0 dB Output Gain the output is the input, bit for bit, at every sample rate and block size")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
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

TEST_CASE ("Output Gain set before the first block applies from the first sample, to every channel")
{
    const int numChannels = GENERATE (1, 2);
    const auto input = test::noise (numChannels, 4096);
    auto output = input;
    Engine engine;
    engine.prepare (48000.0, 512, numChannels);
    engine.setSettings ({ .outputGainDb = -6.0 });
    test::processInBlocks (engine, output, { 512 });

    for (std::size_t ch = 0; ch < input.size(); ++ch)
        for (std::size_t i = 0; i < input[ch].size(); ++i)
            REQUIRE (output[ch][i] == static_cast<float> (test::minus6DbGain * input[ch][i]));
}

TEST_CASE ("A jump in Output Gain glides, with no step larger than the Smoother allows, and arrives exactly")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    // From, to, and the gain the new setting plays: 10^(dB/20), worked out independently.
    const auto [fromDb, toDb, toGain] = GENERATE (table<double, double, double> (
        { { 0.0, 36.0, 63.0957344480193 }, { 0.0, -36.0, 0.0158489319246111 }, { 36.0, 0.0, 1.0 } }));
    CAPTURE (sampleRate, fromDb, toDb);

    // A steady 1 in, so the output is the gain applied.
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings ({ .outputGainDb = fromDb });
    test::Channels before { std::vector<float> (512, 1.0f) };
    test::processInBlocks (engine, before, { 512 });
    const double fromGain = before[0].back();

    engine.setSettings ({ .outputGainDb = toDb });
    test::Channels audio { std::vector<float> (static_cast<std::size_t> (0.2 * sampleRate), 1.0f) };
    test::processInBlocks (engine, audio, { 512 });

    const auto& out = audio[0];
    const double limit = largestGlideStep (toGain - fromGain, sampleRate);
    double previous = fromGain, largest = 0.0;
    for (const float sample : out)
    {
        largest = std::max (largest, std::abs (sample - previous));
        previous = sample;
    }
    CHECK (largest <= limit);
    // It moves at once, but only a small step.
    CHECK (out.front() != static_cast<float> (fromGain));
    // Settled after 200 ms, on the new gain exactly as a steady setting plays it.
    CHECK (out.back() == static_cast<float> (toGain));
}

TEST_CASE ("While Output Gain glides, the output is the same however the audio is cut into blocks")
{
    // The reference plays the audio in blocks of 4096 (wholeCut); that cut is left out, as it would equal the
    // reference by construction.
    const auto cut = GENERATE (filter ([] (const std::vector<int>& c) { return c != wholeCut; }, from_range (test::blockCuts())));
    CAPTURE (cut);
    const auto input = test::noise (2, 20000);
    auto whole = input, cutUp = input;
    for (auto* audio : { &whole, &cutUp })
    {
        Engine engine;
        engine.prepare (48000.0, 4096, 2);
        engine.setSettings ({ .outputGainDb = 0.0 });
        auto lead = test::noise (2, 100, 9);
        test::processInBlocks (engine, lead, { 100 });
        engine.setSettings ({ .outputGainDb = -24.0 });
        test::processInBlocks (engine, *audio, audio == &whole ? wholeCut : cut);
    }
    CHECK (cutUp == whole);
}
