#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr double toneFrequency = 200.0;
constexpr double toneSeconds = 1.5;
constexpr double onsetSeconds = 0.3;
// -12 dB: 6 dB over the default Threshold, so the compressor is working when a jump comes.
constexpr double toneAmplitude = 0.25;

struct Host
{
    double sampleRate;
    int blockSize;
};

// Plays a sine through the Engine as a host would, letting change(seconds, settings) edit the settings before each
// block, and returns the output.
std::vector<float> playTone (Host host, const std::function<void (double, Settings&)>& change)
{
    const auto [sampleRate, blockSize] = host;
    const int numBlocks = static_cast<int> (toneSeconds * sampleRate / blockSize);

    Engine engine;
    engine.prepare (sampleRate, blockSize, 1);

    Settings settings;
    std::vector<float> output;
    std::vector<float> block (static_cast<std::size_t> (blockSize));
    float* channels[] = { block.data() };
    int n = 0;
    for (int b = 0; b < numBlocks; ++b)
    {
        change (static_cast<double> (n) / sampleRate, settings);
        engine.setSettings (settings);
        for (auto& s : block)
            s = static_cast<float> (toneAmplitude * std::sin (2.0 * std::numbers::pi * toneFrequency * n++ / sampleRate));
        engine.process ({ channels, 1, blockSize });
        output.insert (output.end(), block.begin(), block.end());
    }
    return output;
}

// How much sharper the sharpest corner of the output is, from the onset on, than a pure sine of toneFrequency at
// the output's peak level allows: a sine A sin(wn) has a second difference of at most A w^2. Clicks show up far
// above 1. The onset is skipped: the tone itself starts abruptly, and the compressor's first Attack moves with it.
double discontinuity (const std::vector<float>& output, double sampleRate)
{
    double peak = 0.0, sharpest = 0.0;
    const auto from = static_cast<std::size_t> (onsetSeconds * sampleRate);
    for (auto i = std::max (from, std::size_t { 2 }); i < output.size(); ++i)
    {
        peak = std::max (peak, static_cast<double> (std::abs (output[i])));
        sharpest = std::max (sharpest, std::abs (static_cast<double> (output[i]) - 2.0 * output[i - 1] + output[i - 2]));
    }
    const double w = 2.0 * std::numbers::pi * toneFrequency / sampleRate;
    return sharpest / (peak * w * w);
}

constexpr double threshold = 2.0;

// False until the onset has passed, then flips every 50 ms.
bool alternating (double seconds) { return seconds >= onsetSeconds && static_cast<int> ((seconds - onsetSeconds) / 0.05) % 2 == 0; }

// A sample at a time, odd and large blocks, at every sample rate comp1 supports.
Host anyHost()
{
    return GENERATE (Host { 44100.0, 17 }, Host { 48000.0, 64 }, Host { 48000.0, 512 }, Host { 88200.0, 1 }, Host { 96000.0, 17 },
                     Host { 96000.0, 512 }, Host { 176400.0, 33 }, Host { 192000.0, 4096 });
}

constexpr double infinity = std::numeric_limits<double>::infinity();

// A jump in one setting, between the values a and b.
struct Jump
{
    std::string setting;
    std::function<void (Settings&, bool)> set; // sets a when the bool is false, b when true
};

const std::vector<Jump>& jumps()
{
    static const std::vector<Jump> all {
        { "Threshold", [] (Settings& s, bool b) { s.thresholdDb = b ? -40.0 : 0.0; } },
        { "Ratio", [] (Settings& s, bool b) { s.ratio = b ? infinity : 1.0; } },
        { "Knee", [] (Settings& s, bool b) { s.kneeDb = b ? 72.0 : 0.0; } },
        { "Attack", [] (Settings& s, bool b) { s.attackMs = b ? 250.0 : 1.0; } },
        { "Release", [] (Settings& s, bool b) { s.releaseMs = b ? 5000.0 : 20.0; } },
        { "Hold", [] (Settings& s, bool b) { s.holdMs = b ? 500.0 : 0.0; } },
        { "Range", [] (Settings& s, bool b) { s.rangeDb = b ? 60.0 : 0.0; } },
        { "Auto Gain", [] (Settings& s, bool b) { s.autoGain = b; } },
        // Held, the uncompressed input plays in place of the compressed one.
        { "Detection Audition", [] (Settings& s, bool b) { s.detectionAudition = b; } },
    };
    return all;
}

} // namespace

TEST_CASE ("Jumps in each compressor setting do not click")
{
    const auto& jump = GENERATE (from_range (jumps()));
    const auto host = anyHost();
    CAPTURE (jump.setting, host.sampleRate, host.blockSize);
    const auto output = playTone (host, [&jump] (double time, Settings& s) { jump.set (s, alternating (time)); });
    CHECK (discontinuity (output, host.sampleRate) < threshold);
}

TEST_CASE ("While a compressor setting glides, the output is the same however the audio is cut into blocks")
{
    const auto& jump = GENERATE (from_range (jumps()));
    // The reference plays the audio in blocks of 4096; that cut is left out, as it would equal the reference by
    // construction.
    const std::vector<int> wholeCut { 4096 };
    const auto cut = GENERATE_REF (filter ([&wholeCut] (const std::vector<int>& c) { return c != wholeCut; }, from_range (test::blockCuts())));
    CAPTURE (jump.setting, cut);
    // Noise, loud enough to be compressed throughout.
    const auto input = test::noise (2, 20000);
    auto whole = input, cutUp = input;
    for (auto* audio : { &whole, &cutUp })
    {
        Engine engine;
        engine.prepare (48000.0, 4096, 2);
        Settings settings;
        jump.set (settings, false);
        engine.setSettings (settings);
        // The jump lands 100 samples in, part-way through a run.
        auto lead = test::noise (2, 100, 9);
        test::processInBlocks (engine, lead, { 100 });
        jump.set (settings, true);
        engine.setSettings (settings);
        test::processInBlocks (engine, *audio, audio == &whole ? wholeCut : cut);
    }
    CHECK (cutUp == whole);
}
