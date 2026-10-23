#pragma once

#include "comp1/Engine.h"
#include "comp1/Style.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <random>
#include <utility>
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

// Paul Kellet's three-pole pink filter, which turns white noise into pink-like noise: one per channel, its state carried
// from each sample to the next.
struct PinkFilter
{
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;

    // The next pink-like sample, from the next white one, w.
    double operator() (double w)
    {
        b0 = 0.99765 * b0 + 0.0990460 * w;
        b1 = 0.96300 * b1 + 0.2965164 * w;
        b2 = 0.57000 * b2 + 1.0526913 * w;
        return b0 + b1 + b2 + 0.1848 * w;
    }
};

// The RMS level of count samples in dB.
inline double rmsDb (const float* samples, int count)
{
    double power = 0.0;
    for (int i = 0; i < count; ++i)
        power += static_cast<double> (samples[i]) * samples[i];
    return 10.0 * std::log10 (power / count);
}

// Seeded pink-like noise on each channel at an RMS level of rmsLevelDb: white noise (noise()) through a PinkFilter,
// each channel its own, the same for the same seed on every platform.
inline Channels pinkNoise (int numChannels, int numSamples, double rmsLevelDb, std::uint32_t seed)
{
    auto audio = noise (numChannels, numSamples, seed);
    for (auto& channel : audio)
    {
        PinkFilter pink;
        for (auto& x : channel)
            x = static_cast<float> (pink (x));
        const auto gain = static_cast<float> (std::pow (10.0, (rmsLevelDb - rmsDb (channel.data(), numSamples)) / 20.0));
        for (auto& x : channel)
            x *= gain;
    }
    return audio;
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

// A change of settings part-way through what an Engine plays: from sample at on, it plays with settings. at is the
// start of a host block, so the change lands on that sample.
struct SettingsChange
{
    int at = 0;
    Settings settings;
};

// audio through a fresh Engine at sampleRate, with settings from the start (so they apply at once, with no glide), cut
// into blocks as given and with the samples of sidechainSamples, as long as audio, on the Sidechain when it isn't
// null; and from change's sample on, with its settings, when there is one. Each side of the change is cut afresh.
inline Channels played (const Settings& settings,
                        double sampleRate,
                        Channels audio,
                        const std::vector<int>& cut = { 512 },
                        const Channels* sidechainSamples = nullptr,
                        const std::optional<SettingsChange>& change = std::nullopt)
{
    const auto split = [] (const Channels& whole, std::size_t from, std::size_t to) {
        Channels piece;
        for (const auto& channel : whole)
            piece.emplace_back (channel.begin() + static_cast<std::ptrdiff_t> (from), channel.begin() + static_cast<std::ptrdiff_t> (to));
        return piece;
    };
    const std::size_t length = audio.front().size();
    const std::size_t at = change ? std::min (static_cast<std::size_t> (change->at), length) : length;
    Engine engine;
    engine.prepare (sampleRate, *std::max_element (cut.begin(), cut.end()), static_cast<int> (audio.size()));
    engine.setSettings (settings);
    const auto play = [&] (std::size_t from, std::size_t to) {
        auto piece = split (audio, from, to);
        const auto sidechainPiece = sidechainSamples != nullptr ? std::optional { split (*sidechainSamples, from, to) } : std::nullopt;
        if (from < to)
            processInBlocks (engine, piece, cut, sidechainPiece ? &*sidechainPiece : nullptr);
        return piece;
    };
    auto out = play (0, at);
    if (change)
    {
        engine.setSettings (change->settings);
        const auto rest = play (at, length);
        for (std::size_t ch = 0; ch < out.size(); ++ch)
            out[ch].insert (out[ch].end(), rest[ch].begin(), rest[ch].end());
    }
    return out;
}

// Runs audio through engine, already prepared and set up, in place, cut into blocks of the sizes given, in turn and
// repeating; from change's sample on with its settings when there is one, the block before it cut short there. Reads
// the level history after each block, as the editor reads it while the host plays, and returns every record read.
inline std::vector<LevelRecord> playAndReadLevels (Engine& engine,
                                                   Channels& audio,
                                                   const std::vector<int>& blockSizes,
                                                   const std::optional<SettingsChange>& change = std::nullopt)
{
    std::vector<LevelRecord> all, records (static_cast<std::size_t> (engine.levelHistoryCapacity()));
    const int numSamples = static_cast<int> (audio.front().size());
    std::vector<float*> main (audio.size());
    for (int start = 0, block = 0; start < numSamples; ++block)
    {
        if (change && start == change->at)
            engine.setSettings (change->settings);
        int count = std::min (blockSizes[static_cast<std::size_t> (block) % blockSizes.size()], numSamples - start);
        if (change && start < change->at)
            count = std::min (count, change->at - start);
        for (std::size_t ch = 0; ch < audio.size(); ++ch)
            main[ch] = audio[ch].data() + start;
        engine.process ({ main.data(), static_cast<int> (main.size()), count });
        start += count;
        const int read = engine.readLevels (records.data(), static_cast<int> (records.size()));
        all.insert (all.end(), records.begin(), records.begin() + read);
    }
    return all;
}

// The level history of audio through a fresh Engine at sampleRate, with settings from the start, and from change's
// sample on with its settings when there is one: every record, read after each host block of up to 512 samples, as
// the editor reads it while the host plays.
inline std::vector<LevelRecord> levelHistory (const Settings& settings, double sampleRate, Channels audio, const std::optional<SettingsChange>& change = std::nullopt)
{
    constexpr int blockSize = 512;
    Engine engine;
    engine.prepare (sampleRate, blockSize, static_cast<int> (audio.size()));
    engine.setSettings (settings);
    return playAndReadLevels (engine, audio, { blockSize }, change);
}

// audio delayed by samples, the start filled with silence and the end cut off, so it keeps its length.
inline Channels delayed (const Channels& audio, int samples)
{
    auto out = audio;
    for (std::size_t ch = 0; ch < audio.size(); ++ch)
        for (std::size_t i = 0; i < audio[ch].size(); ++i)
            out[ch][i] = i < static_cast<std::size_t> (samples) ? 0.0f : audio[ch][i - static_cast<std::size_t> (samples)];
    return out;
}

// A steady Detection Level on each channel: channel ch a square wave whose peak reads levelsDb[ch], the same magnitude
// every sample, so the peak detector reads that level throughout.
inline Channels steadyLevels (const std::vector<double>& levelsDb, int numSamples)
{
    Channels channels;
    for (const double levelDb : levelsDb)
    {
        const auto magnitude = static_cast<float> (std::pow (10.0, levelDb / 20.0));
        auto& channel = channels.emplace_back (static_cast<std::size_t> (numSamples));
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] = i % 2 == 0 ? magnitude : -magnitude;
    }
    return channels;
}

// The same steady Detection Level, levelDb, on every channel.
inline Channels steadyLevel (int numChannels, int numSamples, double levelDb)
{
    return steadyLevels (std::vector<double> (static_cast<std::size_t> (numChannels), levelDb), numSamples);
}

// A steady level that doesn't alternate: the same sample value, levelDb, every sample. Saturation's ADAA averages the
// residual between neighbouring samples, and a square wave's two values average out (the residual is odd), so a
// steady level through Saturation is this one, as dyn-lab's steady-level curve for Saturation is.
inline Channels constantLevel (int numChannels, int numSamples, double levelDb)
{
    const auto magnitude = static_cast<float> (std::pow (10.0, levelDb / 20.0));
    return Channels (static_cast<std::size_t> (numChannels), std::vector<float> (static_cast<std::size_t> (numSamples), magnitude));
}

// How far a sine's peak is over its RMS level, 10·log10(2) dB (dyn-lab's SINE_CALIBRATION_DB). VCA reads a
// sine-calibrated RMS level, so its steady test signal is a steady level (steadyLevel) this far under the level it
// stands for, and a level read off its output is this far over the square wave's.
inline constexpr double sineCalibrationDb = 3.0102999566398120;

// Every Character, as the tests know it from dyn-lab's model (its LEVELS): how far its steady test signal sits from the
// level it stands for, in dB. VCA reads an RMS level, so its signal is a square wave sineCalibrationDb under the level;
// the others read the peak, so theirs is at it. Written here apart from the Engine's description of each Character.
struct CharacterUnderTest
{
    Character character;
    double steadyLevelOffsetDb;
};

inline const std::vector<CharacterUnderTest>& characters()
{
    static const std::vector<CharacterUnderTest> all {
        { Character::Clean, 0.0 },
        { Character::VCA, -sineCalibrationDb },
        { Character::FET, 0.0 },
        { Character::Opto, 0.0 },
    };
    return all;
}

// How far character's steady test signal sits from the level it stands for, in dB (characters()).
inline double steadyLevelOffsetDb (Character character)
{
    const auto& all = characters();
    return std::find_if (all.begin(), all.end(), [character] (const CharacterUnderTest& c) { return c.character == character; })->steadyLevelOffsetDb;
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
// input levels: a sample held at the new level can read a hair off a start read at the old one (under 2e-7 of the way
// on the smallest step the tests measure, 6 dB). So it has moved once it is more than heldWithin of the way, under the
// first step of any Clean Release (1.04e-6 of the way at 5000 ms and 192 kHz). dyn-lab counts the same way
// (MOVED_WITHIN), so VCA, whose Gain Reduction leaves its level slowly as its RMS level falls, reads the same Hold in
// both.
inline constexpr double heldWithin = 1.0e-6;

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
    const auto output = played (settings, sampleRate, input);
    const double gain = static_cast<double> (output.front().back()) / static_cast<double> (input.front().back());
    for (const auto& channel : output)
        if (channel.back() != output.front().back())
            return std::nan ("");
    return levelDb + 20.0 * std::log10 (gain);
}

// Each channel's output level in dB after seconds of steady levels, levelsDb[ch] on channel ch, through an Engine with
// settings: its input level plus the gain its last sample got.
inline std::vector<double> steadyOutputsDb (const Settings& settings, const std::vector<double>& levelsDb, double sampleRate, double seconds = 0.5)
{
    const auto input = steadyLevels (levelsDb, static_cast<int> (seconds * sampleRate));
    const auto output = played (settings, sampleRate, input);
    std::vector<double> outputDb;
    for (std::size_t ch = 0; ch < input.size(); ++ch)
        outputDb.push_back (levelsDb[ch] + 20.0 * std::log10 (static_cast<double> (output[ch].back()) / static_cast<double> (input[ch].back())));
    return outputDb;
}

// Every Stereo Placement comp1 offers.
inline const std::vector<StereoPlacement>& stereoPlacements()
{
    static const std::vector<StereoPlacement> all { StereoPlacement::Stereo, StereoPlacement::Mid, StereoPlacement::Side };
    return all;
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

// A host: a sample rate, and the block sizes it cuts the audio into, in turn.
struct Host
{
    double sampleRate;
    std::vector<int> cut;
};

// Each sample rate comp1 supports, with a block cut in turn, for a sweep that needs every rate but not every rate at
// every cut.
inline const std::vector<Host>& hosts()
{
    static const std::vector<Host> all = [] {
        std::vector<Host> h;
        for (std::size_t r = 0; r < sampleRates().size(); ++r)
            h.push_back ({ sampleRates()[r], blockCuts()[r % blockCuts().size()] });
        return h;
    }();
    return all;
}

// signal on the left channel and, at half its level, on the right.
inline Channels stereo (const std::vector<float>& signal)
{
    Channels audio { signal, signal };
    for (auto& sample : audio[1])
        sample *= 0.5f;
    return audio;
}

// The loudest sample of every channel, linear.
inline double samplePeak (const Channels& audio)
{
    double peak = 0.0;
    for (const auto& channel : audio)
        for (const float sample : channel)
            peak = std::max (peak, static_cast<double> (std::abs (sample)));
    return peak;
}

// --- Step response -----------------------------------------------------------------------------------------------

// The Gain Reduction in dB the listener hears on each sample, as an Engine with settings at sampleRate plays a steady
// level held for seconds, then another for toSeconds (as long by default): each output sample against the input sample latency samples
// earlier, the one it carries. Index 0 is the first output sample after the first level's seconds, so the step itself
// is heard at index latency.
inline std::vector<double> heardGainReduction (const Settings& settings, double sampleRate, double fromDb, double toDb, double seconds, int latency, double toSeconds = 0.0)
{
    const int length = static_cast<int> (seconds * sampleRate);
    auto audio = steadyLevel (1, length, fromDb);
    const auto after = steadyLevel (1, static_cast<int> ((toSeconds > 0.0 ? toSeconds : seconds) * sampleRate), toDb);
    audio.front().insert (audio.front().end(), after.front().begin(), after.front().end());
    const auto input = audio.front();
    audio = played (settings, sampleRate, audio);
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
    audio = played (settings, sampleRate, audio, { blockSize });
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

// --- The Dry/Wet null -------------------------------------------------------------------------------------------

// A setting the Dry/Wet null must hold under: a row of the null's table, such as a Character at a Colour.
// Oversampling adds one. Each row leaves the Wet path linear and aligned with no Gain Reduction.
struct NullRow
{
    const char* name;
    void (*apply) (Settings&);
};

// The settings that leave the Wet path unchanged (Ratio 1:1, Auto Gain off, Saturation off, Output Gain 0 dB) in mode
// at lookaheadMs, with row applied and Dry and Wet at dryDb and wetDb.
inline Settings nullSettings (const NullRow& row, ProcessingMode mode, double lookaheadMs, double dryDb, double wetDb)
{
    Settings settings { .ratio = 1.0, .autoGain = false, .saturation = false, .outputGainDb = 0.0, .processingMode = mode, .lookaheadMs = lookaheadMs };
    settings.dryDb = dryDb;
    settings.wetDb = wetDb;
    row.apply (settings);
    return settings;
}

// The worst relative error of output against expected times gain, over every sample: 0 when they match exactly.
// A sample whose expected value is 0 counts any output as the whole error.
inline double worstRelativeError (const Channels& output, const Channels& expected, double gain)
{
    double worst = 0.0;
    for (std::size_t ch = 0; ch < expected.size(); ++ch)
        for (std::size_t i = 0; i < expected[ch].size(); ++i)
        {
            const double want = gain * expected[ch][i], got = output[ch][i];
            if (want == got)
                continue;
            worst = std::max (worst, want == 0.0 ? 1.0 : std::abs (got - want) / std::abs (want));
        }
    return worst;
}

// --- Clicks ------------------------------------------------------------------------------------------------------

// How much sharper the sharpest corner of a tone's output is, from sample from on, than a pure sine of toneFrequency
// at the output's peak level allows: a sine A sin(wn) has a second difference of at most A w^2. Clicks show up far
// above 1. On two channels, the worse of the two, each against its own peak.
inline double discontinuity (const Channels& output, double toneFrequency, double sampleRate, std::size_t from)
{
    double worst = 0.0;
    for (const auto& channel : output)
    {
        double peak = 0.0, sharpest = 0.0;
        for (auto i = std::max (from, std::size_t { 2 }); i < channel.size(); ++i)
        {
            peak = std::max (peak, static_cast<double> (std::abs (channel[i])));
            sharpest = std::max (sharpest, std::abs (static_cast<double> (channel[i]) - 2.0 * channel[i - 1] + channel[i - 2]));
        }
        const double w = 2.0 * std::numbers::pi * toneFrequency / sampleRate;
        worst = std::max (worst, sharpest / (peak * w * w));
    }
    return worst;
}

// A discontinuity() at or over this is a click.
inline constexpr double clickLimit = 2.0;

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

// --- Colour, as dyn-lab's `colour` measures it -------------------------------------------------------------------

// dyn-lab's colour_level at Settings' defaults but Threshold: the input level whose Gain Reduction on the Transfer
// Curve is reductionDb, above the Knee from 3 dB of Gain Reduction at the defaults (4 dB over Threshold, past the
// Knee's 3 dB half-width); or for none, 12 dB under the Knee (COLOUR_BELOW_KNEE_DB).
inline double levelForReductionDb (double reductionDb, double thresholdDb = Settings {}.thresholdDb)
{
    const Settings defaults;
    const double slope = 1.0 - 1.0 / defaults.ratio;
    return reductionDb > 0.0 ? thresholdDb + reductionDb / slope : thresholdDb - defaults.kneeDb / 2.0 - 12.0;
}

// The bin-centred tone in bin at levelDb, times scale, for lengths spectrumLengths, through an Engine with settings at
// sampleRate, compressed by a steady Gain Reduction, as dyn-lab's colour holds it: an External Detection Source on a
// Sidechain that is the Character's steady test signal for levelDb (steadyLevelOffsetDb), which it reads at levelDb with
// no ripple at all. The Gain Reduction settles over the first length (Attack 10 ms, VCA's RMS level 5 ms), so the last
// is periodic.
inline std::vector<float> steadilyCompressedTone (Settings settings, double sampleRate, int bin, double levelDb, int lengths, double scale = 1.0)
{
    auto tone = binCentredTone (bin, levelDb, lengths);
    for (auto& sample : tone)
        sample = static_cast<float> (scale * sample);
    const auto sidechain = steadyLevel (1, static_cast<int> (tone.size()), levelDb + steadyLevelOffsetDb (settings.character));
    settings.detectionSource = DetectionSource::External;
    return played (settings, sampleRate, { tone }, { 512 }, &sidechain).front();
}

// --- The Limiter: true peak, and dyn-lab's reference signals (python3 tools/dyn-lab/dynlab.py limiter) ------------

// The true peak of every channel, linear, by dyn-lab's 16x reference: each channel interpolated at 16 points per
// host sample by a Kaiser-windowed sinc (β 10) 64 host samples long, its cutoff the host's Nyquist. Written here
// apart from the Engine's own estimator, which it measures. It reads silence before the audio, and stops at the last
// sample whose interpolation reads nothing past the end, where the audio would go on. From sample from on, when given,
// with the audio before it heard.
inline double truePeak (const Channels& audio, int from = 0)
{
    constexpr int factor = 16, taps = 64, half = taps / 2;
    constexpr double beta = 10.0;
    const auto besselI0 = [] (double x) {
        double total = 1.0, term = 1.0;
        for (int k = 1; term > 1.0e-17 * total; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            total += term;
        }
        return total;
    };
    static const auto phases = [&besselI0] {
        std::vector<std::vector<double>> all;
        for (int p = 1; p < factor; ++p)
        {
            std::vector<double> kernel;
            for (int j = 1 - half; j <= half; ++j)
            {
                const double t = static_cast<double> (p) / factor - j;
                const double u = t / half;
                const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / besselI0 (beta);
                kernel.push_back (std::sin (std::numbers::pi * t) / (std::numbers::pi * t) * window);
            }
            double total = 0.0;
            for (const double c : kernel)
                total += c;
            for (double& c : kernel)
                c /= total;
            all.push_back (kernel);
        }
        return all;
    }();
    // The most any phase can make of samples up to 1 in magnitude: a segment whose window peaks at w can read no more
    // than w times it, so one that can't pass the peak found so far is skipped, which changes nothing it returns.
    static const double mostGain = [] {
        double most = 0.0;
        for (const auto& kernel : phases)
        {
            double sum = 0.0;
            for (const double c : kernel)
                sum += std::abs (c);
            most = std::max (most, sum);
        }
        return most;
    }();
    double peak = 0.0;
    std::vector<double> padded;
    for (const auto& channel : audio)
    {
        const auto length = static_cast<int> (channel.size());
        // half samples of silence before, the channel, then half more, so every window reads in range.
        padded.assign (static_cast<std::size_t> (length + taps), 0.0);
        for (int n = 0; n < length; ++n)
            padded[static_cast<std::size_t> (n + half)] = channel[static_cast<std::size_t> (n)];
        for (int m = from; m + half < length; ++m)
        {
            const double* window = padded.data() + m + 1; // x[m + 1 - half] to x[m + half]
            peak = std::max (peak, std::abs (window[half - 1]));
            double loudest = 0.0;
            for (int j = 0; j < taps; ++j)
                loudest = std::max (loudest, std::abs (window[j]));
            if (loudest * mostGain <= peak)
                continue;
            for (const auto& kernel : phases)
            {
                double sum = 0.0;
                for (int j = 0; j < taps; ++j)
                    sum += kernel[static_cast<std::size_t> (j)] * window[j];
                peak = std::max (peak, std::abs (sum));
            }
        }
    }
    return peak;
}

// A level in dB as a linear gain.
inline double gainOf (double db) { return std::pow (10.0, db / 20.0); }

// How many samples each of dyn-lab's Limiter reference signals runs, before the Engine's latency is added.
inline constexpr int limiterSignalSamples = 2048;

// dyn-lab's fade-in: a raised cosine over 1 ms (LIMITER_FADE_MS), so a signal is band-limited from its first sample.
inline double fadeIn (double sampleRate, int n)
{
    const double samples = 1.0e-3 * sampleRate;
    return n >= samples ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * n / samples);
}

// dyn-lab's limiter_tone: a sine at frequency, its peak at levelDb, faded in, from a phase of 0.3 radians.
inline std::vector<float> limiterTone (double frequency, double levelDb, double sampleRate, int length)
{
    std::vector<float> tone (static_cast<std::size_t> (length));
    for (int n = 0; n < length; ++n)
        tone[static_cast<std::size_t> (n)] = static_cast<float> (gainOf (levelDb) * fadeIn (sampleRate, n)
                                                                 * std::sin (2.0 * std::numbers::pi * frequency * n / sampleRate + 0.3));
    return tone;
}

// dyn-lab's band_limited_noise: 64 sines evenly spaced in log frequency from 20 Hz to 20 kHz, phases from dyn-lab's
// seeded generator (_white), faded in, its sample peak at levelDb.
inline std::vector<float> bandLimitedNoise (double levelDb, double sampleRate, int length, std::uint32_t seed = 1)
{
    constexpr int tones = 64;
    std::uint32_t state = seed;
    std::vector<double> frequencies, phases;
    for (int k = 0; k < tones; ++k)
    {
        state = 1664525u * state + 1013904223u;
        frequencies.push_back (20.0 * std::pow (1000.0, static_cast<double> (k) / (tones - 1)));
        phases.push_back (std::numbers::pi * (state / 2147483648.0 - 1.0));
    }
    std::vector<double> noise (static_cast<std::size_t> (length));
    double peak = 0.0;
    for (int n = 0; n < length; ++n)
    {
        double sum = 0.0;
        for (std::size_t k = 0; k < frequencies.size(); ++k)
            sum += std::sin (2.0 * std::numbers::pi * frequencies[k] * n / sampleRate + phases[k]);
        noise[static_cast<std::size_t> (n)] = sum * fadeIn (sampleRate, n);
        peak = std::max (peak, std::abs (noise[static_cast<std::size_t> (n)]));
    }
    std::vector<float> out;
    for (const double x : noise)
        out.push_back (static_cast<float> (x * gainOf (levelDb) / peak));
    return out;
}

// dyn-lab's full_scale_burst: 256 samples of silence (BURST_START), then 1 kHz at full scale from its peak.
inline constexpr int burstStart = 256;
inline std::vector<float> fullScaleBurst (double sampleRate, int length)
{
    std::vector<float> burst (static_cast<std::size_t> (length));
    for (int n = burstStart; n < length; ++n)
        burst[static_cast<std::size_t> (n)] = static_cast<float> (std::cos (2.0 * std::numbers::pi * 1000.0 * (n - burstStart) / sampleRate));
    return burst;
}

// One of dyn-lab's Limiter reference signals at a Ceiling, the tones and noise 12 dB over it (LIMITER_PUSH_DB),
// length samples long.
struct LimiterSignal
{
    const char* name;
    std::vector<float> (*make) (double ceilingDb, double sampleRate, int length);
};

inline const std::vector<LimiterSignal>& limiterSignals()
{
    static const std::vector<LimiterSignal> all {
        { "full-scale burst", [] (double, double sampleRate, int length) { return fullScaleBurst (sampleRate, length); } },
        { "10 kHz tone", [] (double ceilingDb, double sampleRate, int length) { return limiterTone (10000.0, ceilingDb + 12.0, sampleRate, length); } },
        { "19 kHz tone", [] (double ceilingDb, double sampleRate, int length) { return limiterTone (19000.0, ceilingDb + 12.0, sampleRate, length); } },
        { "noise", [] (double ceilingDb, double sampleRate, int length) { return bandLimitedNoise (ceilingDb + 12.0, sampleRate, length); } },
    };
    return all;
}


// --- The performance scenes: seeded noise bursts, and the settings the CPU budget and the render scenes share -----

// Noise bursts, the CPU budget's input and the render scenes': loud for the first burstLoudSeconds of every
// burstPeriodSeconds and burstQuietDb under that for the rest, so the compressor, the Gate and the Limiter keep moving.
inline constexpr double burstPeriodSeconds = 0.2;
inline constexpr double burstLoudSeconds = 0.1;
inline constexpr double burstQuietDb = -34.0;

// The bursts' gain at a time in seconds: 1 while loud, burstQuietDb under it while quiet.
inline double burstGain (double seconds)
{
    return std::fmod (seconds, burstPeriodSeconds) < burstLoudSeconds ? 1.0 : gainOf (burstQuietDb);
}

// numSamples of seeded full-scale noise bursts on each channel at sampleRate (noise() doubled, -1 to 1), offsetSeconds
// into the bursts' period: the same for the same seed on every platform.
inline Channels bursts (int numChannels, int numSamples, double sampleRate, std::uint32_t seed = 1, double offsetSeconds = 0.0)
{
    auto audio = noise (numChannels, numSamples, seed);
    for (auto& channel : audio)
        for (std::size_t i = 0; i < channel.size(); ++i)
            channel[i] = static_cast<float> (2.0 * channel[i] * burstGain (static_cast<double> (i) / sampleRate + offsetSeconds));
    return audio;
}

// --- Auto Gain's reference signal ----------------------------------------------------------------------------

// dyn-lab's reference signal (tools/dyn-lab/USAGE.md, "autogain"), written again from its description: pink-like noise
// from a 32-bit linear congruential generator (seed 1) through Paul Kellet's three-pole filter (PinkFilter), 0.5 s at
// 48 kHz at each Detection Level from -40 to 0 dB in 2 dB steps, lowest first, each segment at the power of a sine whose
// peak reads its level.
inline constexpr double referenceRate = 48000.0;

inline std::vector<std::vector<float>> referenceSegments()
{
    std::uint32_t state = 1;
    PinkFilter pink;
    std::vector<std::vector<float>> segments;
    for (int levelDb = -40; levelDb <= 0; levelDb += 2)
    {
        std::vector<double> noise (static_cast<std::size_t> (0.5 * referenceRate));
        double power = 0.0;
        for (auto& x : noise)
        {
            state = 1664525u * state + 1013904223u;
            x = pink (state / 2147483648.0 - 1.0);
            power += x * x;
        }
        const double scale = std::sqrt (std::pow (10.0, levelDb / 10.0) / 2.0 / (power / static_cast<double> (noise.size())));
        auto& segment = segments.emplace_back();
        for (const double x : noise)
            segment.push_back (static_cast<float> (x * scale));
    }
    return segments;
}

// The reference signal's long-term level difference through a fresh mono Engine with settings, output minus input, in
// dB, as dyn-lab measures it: each segment's level difference (its power ratio), averaged in dB over the segments. The
// segments play one after another, in blocks of 512.
inline double referenceLevelDifferenceDb (const Settings& settings)
{
    const auto segments = referenceSegments();
    Engine engine;
    engine.prepare (referenceRate, 512, 1);
    engine.setSettings (settings);
    double sumDb = 0.0;
    for (const auto& segment : segments)
    {
        Channels audio { segment };
        processInBlocks (engine, audio, { 512 });
        double powerIn = 0.0, powerOut = 0.0;
        for (std::size_t i = 0; i < segment.size(); ++i)
        {
            powerIn += static_cast<double> (segment[i]) * segment[i];
            powerOut += static_cast<double> (audio.front()[i]) * audio.front()[i];
        }
        sumDb += 10.0 * std::log10 (powerOut / powerIn);
    }
    return sumDb / static_cast<double> (segments.size());
}

// How far Auto Gain may let the reference signal's level move, in dB: dyn-lab's AUTO_GAIN_TOLERANCE, over its Auto Gain
// grid and along every Style's Comp Curve (tools/dyn-lab/USAGE.md, "autogain" and "Comp").
inline constexpr double autoGainToleranceDb = 1.0;

// Every Style, in the frozen order, as the tests know it from dyn-lab's STYLE_DEFAULTS, written here apart from the
// Style table.
inline const std::vector<Style>& everyStyle()
{
    static const std::vector<Style> all { Style::Clean, Style::VCA,        Style::FET, Style::Opto,
                                          Style::Vocal, Style::Instrument, Style::Bus, Style::Drums };
    return all;
}

// The worst case: every stage on, at its most costly. FET with its full Colour, High Quality at 8x Oversampling (the
// cap at each rate: 4x at 96 kHz) with the most Lookahead, Saturation on, the Gate on and closing on the bursts' quiet
// part (its Threshold over burstQuietDb), Dry and Wet both at 0 dB, and the Limiter on and limiting, Output Gain driving
// the bursts' loud part well over its Ceiling, so its true-peak estimator and gain both work on every run.
inline Settings worstCaseSettings()
{
    Settings settings;
    settings.gate.on = true;
    settings.gate.thresholdDb = -20.0;
    settings.character = Character::FET;
    settings.colour = parameterRanges::colour.high;
    settings.saturation = true;
    settings.dryDb = 0.0;
    settings.wetDb = 0.0;
    settings.outputGainDb = 12.0;
    settings.limiter = true;
    settings.processingMode = ProcessingMode::HighQuality;
    settings.lookaheadMs = parameterRanges::lookahead.high;
    settings.oversampling = Oversampling::x8;
    return settings;
}

} // namespace comp1::test
