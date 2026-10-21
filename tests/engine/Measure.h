#pragma once

#include "comp1/Engine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
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

// A steady level that doesn't alternate: the same sample value, levelDb, every sample. Saturation's ADAA averages the
// residual between neighbouring samples, and a square wave's two values average out (the residual is odd), so a
// steady level through Saturation is this one, as dyn-lab's static curve is.
inline Channels constantLevel (int numChannels, int numSamples, double levelDb)
{
    const auto magnitude = static_cast<float> (std::pow (10.0, levelDb / 20.0));
    return Channels (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (numSamples), magnitude));
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
// level makes the steady level: steadyLevel, or constantLevel.
inline double steadyOutputDb (const Settings& settings,
                              double levelDb,
                              double sampleRate,
                              int numChannels = 1,
                              double seconds = 0.5,
                              Channels (*level) (int, int, double) = steadyLevel)
{
    const auto input = level (numChannels, static_cast<int> (seconds * sampleRate), levelDb);
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
    const auto from = static_cast<std::ptrdiff_t> (length);
    const std::vector<float> carried (input.begin() + from - latency, input.end() - latency);
    return gainReductionDb (carried, { audio.front().begin() + from, audio.front().end() });
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

// --- Spectra: aliasing and harmonics, as dyn-lab's `aliasing` measures them ------------------------

// The length of each spectrum, a power of two, as dyn-lab's ALIAS_SAMPLES: a tone of a whole number of cycles over it
// is bin-centred, so a rectangular window has no leakage.
inline constexpr int spectrumLength = 4096;

// The discrete Fourier transform of a power-of-two count of samples: iterative radix-2, in double.
inline std::vector<std::complex<double>> fft (const std::vector<double>& samples)
{
    const std::size_t n = samples.size();
    std::vector<std::complex<double>> out (n);
    for (std::size_t i = 0, j = 0; i < n; ++i)
    {
        out[j] = samples[i];
        // j is i with its bits reversed: add one to j from the top bit down.
        auto bit = n >> 1;
        for (; bit > 0 && (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j |= bit;
    }
    for (std::size_t size = 2; size <= n; size *= 2)
    {
        const std::size_t half = size / 2;
        for (std::size_t k = 0; k < half; ++k)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (size);
            const std::complex<double> twiddle { std::cos (angle), std::sin (angle) };
            for (std::size_t start = 0; start < n; start += size)
            {
                const auto a = out[start + k], b = out[start + k + half] * twiddle;
                out[start + k] = a + b;
                out[start + k + half] = a - b;
            }
        }
    }
    return out;
}

// The bin of the tone nearest frequency at sampleRate over spectrumLength: an odd one, so, for an odd shape, no alias
// of a harmonic lands on a harmonic's own bin (dyn-lab's tone_bin).
inline int toneBin (double frequency, double sampleRate)
{
    const auto k = static_cast<int> (std::lround (frequency * spectrumLength / sampleRate));
    return k % 2 != 0 ? k : k + 1;
}

// A sine of bin whole cycles over spectrumLength, its peak at levelDb, starting at 0, for lengths spectrumLengths.
inline std::vector<float> binCentredTone (int bin, double levelDb, int lengths = 2)
{
    const double amplitude = std::pow (10.0, levelDb / 20.0);
    std::vector<float> tone (static_cast<std::size_t> (lengths * spectrumLength));
    for (std::size_t n = 0; n < tone.size(); ++n)
        tone[n] = static_cast<float> (amplitude * std::sin (2.0 * std::numbers::pi * bin * static_cast<double> (n) / spectrumLength));
    return tone;
}

// Each bin's peak amplitude, DC to Nyquist, over the last spectrumLength samples.
inline std::vector<double> amplitudes (const std::vector<float>& samples)
{
    const std::vector<double> last (samples.end() - spectrumLength, samples.end());
    const auto bins = fft (last);
    std::vector<double> out (spectrumLength / 2 + 1);
    for (std::size_t b = 0; b < out.size(); ++b)
        out[b] = std::abs (bins[b]) * 2.0 / spectrumLength;
    return out;
}

// The worst alias in dB relative to the fundamental, the tone in bin: the loudest bin that holds no in-band harmonic.
inline double worstAliasDb (const std::vector<double>& spectrum, int bin)
{
    double worst = 0.0;
    for (std::size_t b = 0; b < spectrum.size(); ++b)
        if (b % static_cast<std::size_t> (bin) != 0 || b == 0)
            worst = std::max (worst, spectrum[b]);
    return 20.0 * std::log10 (std::max (worst, 1.0e-300) / spectrum[static_cast<std::size_t> (bin)]);
}

} // namespace comp1::test
