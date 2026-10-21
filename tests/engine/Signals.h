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
inline void processInBlocks (Engine& engine, Channels& audio, const std::vector<int>& blockSizes, const Channels* sidechain = nullptr)
{
    const int numSamples = static_cast<int> (audio.front().size());
    std::vector<float*> main (audio.size());
    std::vector<const float*> key (sidechain != nullptr ? sidechain->size() : 0);
    for (int start = 0, block = 0; start < numSamples; ++block)
    {
        const int count = std::min (blockSizes[static_cast<std::size_t> (block) % blockSizes.size()], numSamples - start);
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            main[ch] = audio[ch].data() + start;
        for (std::size_t ch = 0; ch < key.size(); ++ch)
            key[ch] = (*sidechain)[ch].data() + start;
        const ConstAudioBlock keyBlock { key.data(), static_cast<int> (key.size()), count };
        engine.process ({ main.data(), static_cast<int> (main.size()), count }, sidechain != nullptr ? &keyBlock : nullptr);
        start += count;
    }
}

// Host block sizes, from a sample at a time to large, with odd sizes that cut the Engine's 16-sample runs.
inline const std::vector<std::vector<int>>& blockCuts()
{
    static const std::vector<std::vector<int>> cuts { { 512 }, { 1 }, { 7 }, { 16 }, { 17, 3, 64, 1000 }, { 4096 }, { 33, 1, 15, 128 } };
    return cuts;
}

} // namespace comp1::test
