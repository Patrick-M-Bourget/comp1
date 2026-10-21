#pragma once

#include "comp1/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace comp1::test
{

// Audio as the tests hold it: one vector of samples per channel.
using Channels = std::vector<std::vector<float>>;

// Seeded white noise in -0.5 to 0.5, the same for the same seed on every platform (the distribution is worked
// out here, not by the standard library, whose distributions differ between implementations).
inline Channels noise (int numChannels, int numSamples, std::uint32_t seed = 1)
{
    std::mt19937 random (seed);
    Channels channels (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (numSamples)));
    for (auto& channel : channels)
        for (auto& sample : channel)
            sample = static_cast<float> (random() >> 8) / static_cast<float> (1 << 24) - 0.5f;
    return channels;
}

// Runs audio through engine in place, cut into blocks of the sizes given, in turn and repeating, with the samples of
// sidechainSamples, as long as audio, on the Sidechain when it isn't null.
inline void processInBlocks (Engine& engine, Channels& audio, const std::vector<int>& blockSizes, const Channels* sidechainSamples = nullptr)
{
    const int numSamples = static_cast<int> (audio.front().size());
    std::vector<float*> main (audio.size());
    std::vector<const float*> sidechain (sidechainSamples != nullptr ? sidechainSamples->size() : 0);
    for (int start = 0, block = 0; start < numSamples; ++block)
    {
        const int count = std::min (blockSizes[static_cast<std::size_t> (block) % blockSizes.size()], numSamples - start);
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            main[ch] = audio[ch].data() + start;
        for (std::size_t ch = 0; ch < sidechain.size(); ++ch)
            sidechain[ch] = (*sidechainSamples)[ch].data() + start;
        const ConstAudioBlock sidechainBlock { sidechain.data(), static_cast<int> (sidechain.size()), count };
        engine.process ({ main.data(), static_cast<int> (main.size()), count }, sidechainSamples != nullptr ? &sidechainBlock : nullptr);
        start += count;
    }
}

// A steady Detection Level: a square wave whose peak reads levelDb, the same magnitude every sample, so the peak
// detector reads levelDb throughout.
inline Channels steadyLevel (int numChannels, int numSamples, double levelDb)
{
    const auto magnitude = static_cast<float> (std::pow (10.0, levelDb / 20.0));
    Channels channels (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (numSamples)));
    for (auto& channel : channels)
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] = i % 2 == 0 ? magnitude : -magnitude;
    return channels;
}

// The Gain Reduction in dB the Engine gave each sample: how far the output is under the input.
inline std::vector<double> gainReductionDb (const std::vector<float>& input, const std::vector<float>& output)
{
    std::vector<double> reduction;
    for (std::size_t i = 0; i < input.size(); ++i)
        reduction.push_back (-20.0 * std::log10 (static_cast<double> (output[i]) / static_cast<double> (input[i])));
    return reduction;
}

// The fractions of a move the timing tests read, as dyn-lab does: one time constant (1 - 1/e), and ln 10 of them.
inline constexpr double oneTimeConstant = 1.0 - 0.36787944117144233;
inline constexpr double ninetyPercent = 0.9;

// As dyn-lab measures a step: the samples taken, from the first one at the new level (reduction[from]), for the Gain
// Reduction to move each fraction of the way from start to end, and the samples before it first moved at all. The
// output is float, so a Gain Reduction read off it carries up to about 1e-6 dB of rounding, which differs between
// input levels: a sample held at the new level can read a hair off a start read at the old one. So it has moved once
// it is more than heldWithin of the way, far under the first step of any Release the tests measure (1e-5 of the
// way at 500 ms and 192 kHz).
inline constexpr double heldWithin = 3.0e-6;

struct Crossings
{
    std::optional<int> sixtyThree, ninety;
    int held = -1;
};

inline Crossings crossings (const std::vector<double>& reduction, double start, double end, std::size_t from = 0)
{
    Crossings c;
    for (int n = 1; from + static_cast<std::size_t> (n) <= reduction.size() && ! c.ninety; ++n)
    {
        const double moved = (reduction[from + static_cast<std::size_t> (n - 1)] - start) / (end - start);
        if (c.held < 0 && moved > heldWithin)
            c.held = n - 1;
        if (! c.sixtyThree && moved >= oneTimeConstant)
            c.sixtyThree = n;
        if (! c.ninety && moved >= ninetyPercent)
            c.ninety = n;
    }
    return c;
}

// The output level in dB, on every channel, after seconds of a steady level through an Engine with settings: the
// input level plus the gain the last sample got. NaN when the channels differ.
inline double steadyOutputDb (const Settings& settings, double levelDb, double sampleRate, int numChannels = 1, double seconds = 0.5)
{
    const auto input = steadyLevel (numChannels, static_cast<int> (seconds * sampleRate), levelDb);
    auto output = input;
    Engine engine;
    engine.prepare (sampleRate, 512, numChannels);
    engine.setSettings (settings);
    processInBlocks (engine, output, { 512 });
    const double gain = static_cast<double> (output.front().back()) / static_cast<double> (input.front().back());
    for (const auto& channel : output)
        if (channel.back() != output.front().back())
            return std::nan ("");
    return levelDb + 20.0 * std::log10 (gain);
}

// Every sample rate comp1 supports: plugkit.conf's SAMPLE_RATES, which pluginval runs at, as tests/CMakeLists.txt
// reads it.
inline const std::vector<double>& sampleRates()
{
    static const std::vector<double> rates { COMP1_SAMPLE_RATES };
    return rates;
}

// The gain of -6 dB, 10^(-6/20), worked out independently of the Engine.
inline constexpr double minus6DbGain = 0.501187233627272;

// Host block sizes, from a sample at a time to large, with odd sizes that cut the Engine's 16-sample runs.
inline const std::vector<std::vector<int>>& blockCuts()
{
    static const std::vector<std::vector<int>> cuts { { 512 }, { 1 }, { 7 }, { 16 }, { 17, 3, 64, 1000 }, { 4096 }, { 33, 1, 15, 128 } };
    return cuts;
}

} // namespace comp1::test
