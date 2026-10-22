// Comp and Comp Follow in the Engine (ADR 0004): while Comp Follow is on, the compressor takes Threshold and Ratio from
// the Comp Curve at Comp, with Auto Gain on, and ignores the stored Threshold, Ratio and Auto Gain; turning it on or
// off glides over dyn-lab's fixed time.

#include "Measure.h"
#include "comp1/Style.h"

#include <plugkit/RunGrid.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace comp1;

namespace
{

// A Style's settings following its Comp Curve at comp, through the compressor alone: Colour 0% and the Gate and the
// Limiter off (Vocal's are on), so the output's gain is the compressor's and Auto Gain's alone, as dyn-lab's compcurve
// measures it. The stored Threshold, Ratio and Auto Gain are moved well off the Comp Curve, so a test that reads the
// Comp Curve's values can't be reading theirs.
Settings following (Style style, double comp)
{
    auto settings = styled (style);
    settings.comp = comp;
    settings.colour = 0.0;
    settings.gate.on = false;
    settings.limiter = false;
    settings.thresholdDb = -50.0;
    settings.ratio = 1.5;
    settings.autoGain = false;
    return settings;
}

// A steady input level in dB, and the output level dyn-lab's Transfer Curve gives for it at a point of a Style's Comp Curve.
struct Row
{
    double inputDb;
    double outputDb;
};

// A point of a Style's Comp Curve, and its steady levels: `python3 tools/dyn-lab/dynlab.py transfer --style <name>
// --threshold <T> --ratio <R>` at the Threshold and Ratio `compcurve --style <name>` prints for the Comp, each
// output with the Auto Gain estimate compcurve prints added.
struct CurvePoint
{
    std::string command;
    Style style;
    double comp;
    double autoGainDb;
    std::vector<Row> transfer;
};

const std::vector<CurvePoint>& curvePoints()
{
    static const std::vector<CurvePoint> all {
        { "transfer --style clean --threshold -25.2 --ratio 5", Style::Clean, 70.0, 5.9450, { { -40.0, -40.00 }, { -20.0, -24.16 }, { 0.0, -20.16 } } },
        { "transfer --style clean --threshold -36 --ratio 8", Style::Clean, 100.0, 13.3268, { { -40.0, -40.00 }, { -20.0, -34.00 }, { 0.0, -31.50 } } },
        { "transfer --style vca", Style::VCA, 50.0, 3.5473, { { -40.0, -40.00 }, { -20.0, -20.00 }, { 0.0, -13.50 } } },
        { "transfer --style vca --threshold -36 --ratio 10", Style::VCA, 100.0, 15.4132, { { -40.0, -40.00 }, { -20.0, -34.40 }, { 0.0, -32.40 } } },
        { "transfer --style fet", Style::FET, 50.0, 4.4837, { { -40.0, -40.00 }, { -20.0, -20.00 }, { 0.0, -13.50 } } },
        { "transfer --style fet --threshold -28 --ratio 20", Style::FET, 100.0, 11.8771, { { -40.0, -40.00 }, { -20.0, -27.60 }, { 0.0, -26.60 } } },
        { "transfer --style opto", Style::Opto, 50.0, 3.0868, { { -40.0, -40.00 }, { -20.0, -20.44 }, { 0.0, -12.00 } } },
        { "transfer --style opto --threshold -37.2 --ratio 5", Style::Opto, 90.0, 14.3142, { { -40.0, -40.34 }, { -20.0, -33.76 }, { 0.0, -29.76 } } },
        { "transfer --style vocal", Style::Vocal, 50.0, 3.2261, { { -40.0, -40.00 }, { -20.0, -20.06 }, { 0.0, -12.00 } } },
        { "transfer --style vocal --threshold -36 --ratio 6", Style::Vocal, 100.0, 14.4238, { { -40.0, -40.00 }, { -20.0, -33.33 }, { 0.0, -30.00 } } },
        { "transfer --style instrument", Style::Instrument, 50.0, 3.3922, { { -40.0, -40.00 }, { -20.0, -20.06 }, { 0.0, -13.50 } } },
        { "transfer --style instrument --threshold -36 --ratio 8", Style::Instrument, 100.0, 14.6164, { { -40.0, -40.00 }, { -20.0, -34.00 }, { 0.0, -31.50 } } },
        { "transfer --style bus", Style::Bus, 50.0, 2.3768, { { -40.0, -40.00 }, { -20.0, -20.04 }, { 0.0, -9.00 } } },
        { "transfer --style bus --threshold -30 --ratio 4", Style::Bus, 100.0, 9.1187, { { -40.0, -40.00 }, { -20.0, -27.50 }, { 0.0, -22.50 } } },
        { "transfer --style drums", Style::Drums, 50.0, 3.3791, { { -40.0, -40.00 }, { -20.0, -20.00 }, { 0.0, -13.50 } } },
        { "transfer --style drums --threshold -30 --ratio 10", Style::Drums, 100.0, 10.5974, { { -40.0, -40.00 }, { -20.0, -29.00 }, { 0.0, -27.00 } } },
    };
    return all;
}

// The compressor's Gain Reduction in dB at the end of each run (the level history's), as an Engine at sampleRate plays
// a steady 0 dB level for seconds in blocks of blockSize, with settingsAt (time in seconds) handed over before each.
std::vector<double> gainReductions (const std::function<Settings (double)>& settingsAt, double sampleRate, double seconds, int blockSize = 64)
{
    Engine engine;
    engine.prepare (sampleRate, blockSize, 1);
    const auto length = static_cast<int> (seconds * sampleRate);
    auto audio = test::steadyLevel (1, length, 0.0);
    std::vector<double> reductions;
    std::vector<LevelRecord> records (static_cast<std::size_t> (engine.levelHistoryCapacity()));
    for (int start = 0; start < length; start += blockSize)
    {
        engine.setSettings (settingsAt (start / sampleRate));
        float* channels[] = { audio.front().data() + start };
        engine.process ({ channels, 1, std::min (blockSize, length - start) });
        const int read = engine.readLevels (records.data(), static_cast<int> (records.size()));
        for (int r = 0; r < read; ++r)
            reductions.push_back (records[static_cast<std::size_t> (r)].gainReductionDb);
    }
    return reductions;
}

// The run a time in seconds falls in.
std::size_t runAt (double seconds, double sampleRate) { return static_cast<std::size_t> (seconds * sampleRate) / plugkit::runSamples; }

// Clean at Comp 100%, the Comp Curve's deepest (-36 dB, 8:1), and the stored settings compressing nothing (Ratio 1:1): a
// steady 0 dB level has 31.5 dB of Gain Reduction on the Comp Curve and none off it. Attack and Release are quick (1 and
// 10 ms), so the Gain Reduction follows the settings' glide, not the detector's own times.
Settings farFromTheCurve (bool compFollow)
{
    Settings settings;
    settings.attackMs = 1.0;
    settings.releaseMs = 10.0;
    settings.comp = 100.0;
    settings.compFollow = compFollow;
    settings.thresholdDb = 0.0;
    settings.ratio = 1.0;
    return settings;
}
constexpr double onTheCurveDb = 31.5;

constexpr double switchSeconds = 0.1;
constexpr double glideSeconds = compFollowGlideMs / 1000.0;
// The usual settle after a glide's end: the 50 ms every setting glides over, and Attack's and Release's own times.
constexpr double settleSeconds = 0.1;

} // namespace

TEST_CASE ("Following, each Style's steady Transfer Curve is dyn-lab's at the Comp Curve's Threshold and Ratio, with Auto Gain on", "[sweep]")
{
    const auto& point = GENERATE (from_range (curvePoints()));
    CAPTURE (point.command);
    const auto settings = following (point.style, point.comp);
    for (const auto& row : point.transfer)
    {
        CAPTURE (row.inputDb);
        // VCA reads an RMS level, so its steady test signal sits sineCalibrationDb under the level it stands for.
        const double offsetDb = test::steadyLevelOffsetDb (characterOf (point.style));
        const double outputDb = test::steadyOutputDb (settings, row.inputDb + offsetDb, 48000.0) - offsetDb;
        CHECK (std::abs (outputDb - (row.outputDb + point.autoGainDb)) <= 0.01);
    }
}

TEST_CASE ("Following, moving the stored Threshold, Ratio and Auto Gain leaves the output bit for bit as it was")
{
    const auto style = GENERATE (from_range (test::everyStyle()));
    CAPTURE (static_cast<int> (style));
    const auto input = test::noise (2, 24000);
    const auto settings = following (style, 70.0);
    auto moved = settings;
    moved.thresholdDb = -5.0;
    moved.ratio = parameterRanges::ratio.high;
    moved.autoGain = true;
    const auto played = test::played (settings, 48000.0, input);
    CHECK (test::played (moved, 48000.0, input) == played);
    // Moved part-way through, mid-compression.
    CHECK (test::played (settings, 48000.0, input, { 512 }, nullptr, test::SettingsChange { .at = 9000, .settings = moved }) == played);
}

TEST_CASE ("Following each Style's Comp Curve, Auto Gain keeps the reference signal's level within dyn-lab's tolerance", "[sweep]")
{
    const auto style = GENERATE (from_range (test::everyStyle()));
    for (int comp = 0; comp <= 100; comp += 10)
    {
        CAPTURE (static_cast<int> (style), comp);
        const double differenceDb = test::referenceLevelDifferenceDb (following (style, comp));
        CAPTURE (differenceDb);
        CHECK (std::abs (differenceDb) <= test::autoGainToleranceDb);
    }
}

TEST_CASE ("Turning Comp Follow on glides onto the Comp Curve over the fixed glide time, not the usual 50 ms")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    const auto reductions = gainReductions ([] (double t) { return farFromTheCurve (t >= switchSeconds); }, sampleRate, 0.8);
    CHECK (reductions[runAt (switchSeconds, sampleRate) - 1] == 0.0);
    // Half way, a smoothstep's share is a half: Threshold -18 dB and 1/Ratio half way, about 7.9 dB of Gain Reduction.
    const double halfway = reductions[runAt (switchSeconds + glideSeconds / 2.0, sampleRate)];
    CAPTURE (halfway);
    CHECK (onTheCurveDb - halfway > 0.6 * onTheCurveDb);
    CHECK (std::abs (reductions[runAt (switchSeconds + glideSeconds + settleSeconds, sampleRate)] - onTheCurveDb) <= 0.05);
}

TEST_CASE ("Turning Comp Follow off glides back to the stored settings over the same time")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    const auto reductions = gainReductions ([] (double t) { return farFromTheCurve (t < switchSeconds); }, sampleRate, 0.8);
    CHECK (std::abs (reductions[runAt (switchSeconds, sampleRate) - 1] - onTheCurveDb) <= 0.05);
    const double halfway = reductions[runAt (switchSeconds + glideSeconds / 2.0, sampleRate)];
    CAPTURE (halfway);
    CHECK (halfway > 0.2 * onTheCurveDb);
    CHECK (reductions[runAt (switchSeconds + glideSeconds + settleSeconds, sampleRate)] <= 0.05);
}

TEST_CASE ("Comp Follow turned back mid-glide turns round from where it is, without a jump")
{
    const double sampleRate = GENERATE (from_range (test::sampleRates()));
    CAPTURE (sampleRate);
    const double flipSeconds = switchSeconds + glideSeconds / 2.0;
    const auto reductions = gainReductions ([flipSeconds] (double t) { return farFromTheCurve (t >= switchSeconds && t < flipSeconds); }, sampleRate, 0.8);
    // It gets about a fifth of the way (a smoothstep half way, 7.9 dB), never near the Comp Curve, and comes back down to
    // none.
    const auto most = *std::max_element (reductions.begin(), reductions.end());
    CAPTURE (most);
    CHECK (most > 0.15 * onTheCurveDb);
    CHECK (most < 0.5 * onTheCurveDb);
    CHECK (reductions[runAt (flipSeconds + glideSeconds + settleSeconds, sampleRate)] <= 0.05);
    // No run moves the Gain Reduction by more than a glide over the whole span would at its steepest, 1.5 times the
    // span over the glide time, per run, with room.
    const double steepestDbPerRun = 1.5 * onTheCurveDb / (glideSeconds * sampleRate / plugkit::runSamples);
    for (std::size_t r = 1; r < reductions.size(); ++r)
    {
        CAPTURE (r);
        CHECK (std::abs (reductions[r] - reductions[r - 1]) <= 2.0 * steepestDbPerRun);
    }
}

TEST_CASE ("A Comp Follow glide, and one turned back, play the same at every block size and on every playback", "[sweep]")
{
    // On at sample 4800, back off at 12000, mid-glide, and on again at 28800: each switch lands on its sample whatever
    // the cut, the audio between them cut afresh.
    const auto input = test::noise (2, 48000);
    const std::vector<std::pair<std::size_t, bool>> switches { { 0, false }, { 4800, true }, { 12000, false }, { 28800, true } };
    const auto play = [&input, &switches] (const std::vector<int>& cut) {
        Engine engine;
        engine.prepare (48000.0, *std::max_element (cut.begin(), cut.end()), 2);
        auto audio = input;
        for (std::size_t s = 0; s < switches.size(); ++s)
        {
            const auto from = switches[s].first;
            const auto to = s + 1 < switches.size() ? switches[s + 1].first : audio.front().size();
            engine.setSettings (farFromTheCurve (switches[s].second));
            test::Channels piece;
            for (const auto& channel : audio)
                piece.emplace_back (channel.begin() + static_cast<std::ptrdiff_t> (from), channel.begin() + static_cast<std::ptrdiff_t> (to));
            test::processInBlocks (engine, piece, cut);
            for (std::size_t ch = 0; ch < audio.size(); ++ch)
                std::copy (piece[ch].begin(), piece[ch].end(), audio[ch].begin() + static_cast<std::ptrdiff_t> (from));
        }
        return audio;
    };
    const auto reference = play ({ 4096 });
    CHECK (play ({ 4096 }) == reference);
    for (const auto& cut : test::blockCuts())
    {
        CAPTURE (cut);
        CHECK (play (cut) == reference);
    }
}
