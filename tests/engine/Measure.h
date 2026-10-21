#pragma once

#include "comp1/Engine.h"

#include <algorithm>
#include <cstdint>
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

// Runs audio through engine in place, cut into blocks of the sizes given, in turn and repeating.
inline void processInBlocks (Engine& engine, Channels& audio, const std::vector<int>& blockSizes)
{
    const int numSamples = static_cast<int> (audio.front().size());
    std::vector<float*> main (audio.size());
    for (int start = 0, block = 0; start < numSamples; ++block)
    {
        const int count = std::min (blockSizes[static_cast<std::size_t> (block) % blockSizes.size()], numSamples - start);
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            main[ch] = audio[ch].data() + start;
        engine.process ({ main.data(), static_cast<int> (main.size()), count });
        start += count;
    }
}

// Every sample rate comp1 supports (plugkit.conf's SAMPLE_RATES, which pluginval runs at).
inline const std::vector<double>& sampleRates()
{
    static const std::vector<double> rates { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
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
