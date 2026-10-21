#pragma once

#include "comp1/Engine.h"

#include <algorithm>
#include <cmath>
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

// --- Step response -----------------------------------------------------------------------------------------------

// The Gain Reduction in dB the listener hears on each sample, as an Engine with settings at sampleRate plays a steady
// level held for seconds, then another for as long: each output sample against the input sample latency samples
// earlier, the one it carries. Index 0 is the first output sample after the first level's seconds, so the step itself
// is heard at index latency.
inline std::vector<double> heardGainReduction (const Settings& settings, double sampleRate, double fromDb, double toDb, double seconds, int latency)
{
    const int length = static_cast<int> (seconds * sampleRate);
    auto audio = steadyLevel (1, length, fromDb);
    const auto after = steadyLevel (1, length, toDb);
    audio.front().insert (audio.front().end(), after.front().begin(), after.front().end());
    const auto input = audio.front();
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings (settings);
    processInBlocks (engine, audio, { 512 });
    std::vector<double> reduction;
    for (auto i = static_cast<std::size_t> (length); i < input.size(); ++i)
    {
        const auto carried = static_cast<double> (input[i - static_cast<std::size_t> (latency)]);
        reduction.push_back (-20.0 * std::log10 (static_cast<double> (audio.front()[i]) / carried));
    }
    return reduction;
}

// --- Delay -------------------------------------------------------------------------------------------------------

// How many samples an Engine with settings at sampleRate delays an impulse by, played in blocks of blockSize: where
// the output's largest sample falls, on every channel, or -1 when the channels disagree. The settings should apply
// no Gain Reduction (Ratio 1:1, say), so the impulse comes out whole.
inline int impulseDelaySamples (const Settings& settings, double sampleRate, int blockSize = 512, int numChannels = 2)
{
    const int length = static_cast<int> (0.05 * sampleRate);
    Channels audio (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (length)));
    for (auto& channel : audio)
        channel[100] = 0.5f;
    Engine engine;
    engine.prepare (sampleRate, blockSize, numChannels);
    engine.setSettings (settings);
    processInBlocks (engine, audio, { blockSize });
    int delay = -1;
    for (const auto& channel : audio)
    {
        const auto peak = static_cast<int> (std::max_element (channel.begin(), channel.end(), [] (float a, float b) {
                                                return std::abs (a) < std::abs (b);
                                            }) - channel.begin());
        if (delay >= 0 && peak - 100 != delay)
            return -1;
        delay = peak - 100;
    }
    return delay;
}

} // namespace comp1::test
