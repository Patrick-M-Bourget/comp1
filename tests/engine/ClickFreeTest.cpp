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

// The tone's amplitude at each moment, in seconds.
using Envelope = std::function<double (double)>;

// Plays a sine through the Engine as a host would, letting change(seconds, settings) edit the settings before each
// block, and returns the output.
std::vector<float> playTone (Host host,
                             const std::function<void (double, Settings&)>& change,
                             const Envelope& amplitude = [] (double) { return toneAmplitude; },
                             double seconds = toneSeconds)
{
    const auto [sampleRate, blockSize] = host;
    const int numBlocks = static_cast<int> (seconds * sampleRate / blockSize);

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
        {
            const double time = static_cast<double> (n) / sampleRate;
            s = static_cast<float> (amplitude (time) * std::sin (2.0 * std::numbers::pi * toneFrequency * n++ / sampleRate));
        }
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
    // When it has any, the jump is measured against the steady tone with each of these settings, rather than against
    // a pure sine: Saturation's own harmonics sharpen the tone's corners past a sine's.
    std::vector<std::function<void (Settings&)>> steady = {};
};

// Saturation on at Drive 24 dB, which pushes the tone (-12 dB) 12 dB into the shape.
void saturating (Settings& s)
{
    s.saturation = true;
    s.driveDb = 24.0;
}

const std::vector<Jump>& jumps()
{
    static const std::vector<Jump> all {
        { "Threshold", [] (Settings& s, bool b) { s.thresholdDb = b ? -40.0 : 0.0; } },
        { "Ratio", [] (Settings& s, bool b) { s.ratio = b ? infinity : 1.0; } },
        { "Knee", [] (Settings& s, bool b) { s.kneeDb = b ? 72.0 : 0.0; } },
        { "Attack", [] (Settings& s, bool b) { s.attackMs = b ? 250.0 : 1.0; } },
        { "Release", [] (Settings& s, bool b) { s.releaseMs = b ? 5000.0 : 20.0; } },
        { "Auto Release", [] (Settings& s, bool b) { s.autoRelease = b; } },
        { "Hold", [] (Settings& s, bool b) { s.holdMs = b ? 500.0 : 0.0; } },
        { "Range", [] (Settings& s, bool b) { s.rangeDb = b ? 60.0 : 0.0; } },
        { "Auto Gain", [] (Settings& s, bool b) { s.autoGain = b; } },
        { "Saturation",
          [] (Settings& s, bool b) {
              saturating (s);
              s.saturation = b;
          },
          { saturating } },
        { "Drive",
          [] (Settings& s, bool b) {
              saturating (s);
              s.driveDb = b ? 24.0 : 0.0;
          },
          { saturating } },
        { "Saturation Position",
          [] (Settings& s, bool b) {
              saturating (s);
              s.saturationPosition = b ? SaturationPosition::Before : SaturationPosition::After;
          },
          { [] (Settings& s) {
               saturating (s);
               s.saturationPosition = SaturationPosition::Before;
           },
            saturating } },
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
    double steady = 1.0;
    for (const auto& settings : jump.steady)
        steady = std::max (steady, discontinuity (playTone (host, [&settings] (double, Settings& s) { settings (s); }), host.sampleRate));
    CAPTURE (steady);
    CHECK (discontinuity (output, host.sampleRate) < threshold * steady);
}

namespace
{

// Mid-release, where only Release moves the gain. Each cycle a tone holds 9 dB of Gain Reduction for loudSeconds,
// long enough to charge Auto Release's sustain state most of the way, then fades over fadeSeconds to 1 dB under
// Threshold, so the gain comes back at Release's pace, and fades back up after quietSeconds. The Ratio is ∞:1 and the
// Knee hard, so the loud tone is held at Threshold and the quiet one asks for no Gain Reduction; Attack is slow enough
// not to put a corner of its own in the gain as the tone comes back. Each cycle is a whole number of periods and an
// eighth, so the setting jumps at a different point of the tone each time.
//
// A jump in how fast the gain comes back leaves the gain where it is but bends its slope: a corner, sharpest against
// the tone at the high sample rates and the shortest Release. On the steady tone of the test above, Release never
// gets to give much back, so a bare Release jump passes there; here it clicks at 176.4 and 192 kHz.
constexpr double midReleaseThresholdDb = -24.0;
constexpr double loudSeconds = 1.0, fadeSeconds = 0.005, quietSeconds = 0.200625;
constexpr double cycleSeconds = loudSeconds + 2.0 * fadeSeconds + quietSeconds;
constexpr int cycles = 8;

// The tone's amplitude through the cycles: loud, fading down, quiet, fading up, along raised cosines.
double midReleaseAmplitude (double seconds)
{
    const double loud = std::pow (10.0, (midReleaseThresholdDb + 9.0) / 20.0);
    const double quiet = std::pow (10.0, (midReleaseThresholdDb - 1.0) / 20.0);
    const double t = std::fmod (seconds, cycleSeconds);
    const auto fade = [] (double from, double to, double x) { return from + (to - from) * (1.0 - std::cos (std::numbers::pi * x)) / 2.0; };
    if (t < loudSeconds)
        return loud;
    if (t < loudSeconds + fadeSeconds)
        return fade (loud, quiet, (t - loudSeconds) / fadeSeconds);
    if (t < loudSeconds + fadeSeconds + quietSeconds)
        return quiet;
    return fade (quiet, loud, (t - loudSeconds - fadeSeconds - quietSeconds) / fadeSeconds);
}

// True from the end of each fade down to the start of the next fade up: the setting jumps as the gain starts coming
// back, and jumps back once it has.
bool midRelease (double seconds)
{
    const double t = std::fmod (seconds, cycleSeconds);
    return t >= loudSeconds + fadeSeconds && t < loudSeconds + fadeSeconds + quietSeconds;
}

struct MidRelease
{
    std::string setting;
    std::function<void (Settings&, bool, bool)> set; // sets it for whether it is mid-release, and whether that just began
};

const std::vector<MidRelease>& midReleaseJumps()
{
    static const std::vector<MidRelease> all {
        // From the longest Release to the shortest as the gain starts coming back, and back once it has.
        { "Release", [] (Settings& s, bool midRelease, bool) { s.releaseMs = midRelease ? 5.0 : 5000.0; } },
        // Auto Release on, then off, in turn, as the gain starts coming back: from Release to up to 5 times it, and
        // back.
        { "Auto Release",
          [] (Settings& s, bool, bool began) {
              s.releaseMs = 20.0;
              if (began)
                  s.autoRelease = ! s.autoRelease;
          } },
    };
    return all;
}

} // namespace

TEST_CASE ("Jumps in Release and Auto Release mid-release do not click")
{
    const auto& jump = GENERATE (from_range (midReleaseJumps()));
    const auto host = anyHost();
    CAPTURE (jump.setting, host.sampleRate, host.blockSize);
    bool wasMidRelease = false;
    const auto output = playTone (
        host,
        [&jump, &wasMidRelease] (double time, Settings& s) {
            s.thresholdDb = midReleaseThresholdDb;
            s.ratio = infinity;
            s.kneeDb = 0.0;
            s.attackMs = 50.0;
            const bool now = midRelease (time);
            jump.set (s, now, now && ! wasMidRelease);
            wasMidRelease = now;
        },
        midReleaseAmplitude,
        cycles * cycleSeconds);
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
