#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/Placement.h>

#include <array>
#include <cmath>
#include <optional>
#include <limits>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr double sampleRate = 48000.0;
// Lengths of spectrumLength the coloured tone plays for, the last measured, as the lab measures its second.
constexpr int lengths = 8;
constexpr double absent = -std::numeric_limits<double>::infinity();

// A bin-centred sine near 1 kHz at levelDb, compressed on settings' Character by a steady Gain Reduction.
std::vector<float> colouredTone (const Settings& settings, double levelDb, double scale = 1.0)
{
    return test::steadilyCompressedTone (settings, sampleRate, test::toneBin (1000.0, sampleRate), levelDb, lengths, scale);
}

// One row of `colour`: the steady Gain Reduction, and the 2nd to 5th harmonics in dB relative to the fundamental.
struct Row
{
    double reductionDb;
    std::array<double, 4> harmonicsDb;
};

struct Table
{
    std::string command; // the dyn-lab command that printed rows
    Character character;
    double colour;
    std::vector<Row> rows;
    // In High Quality at this factor, or with x1 at the base rate in both Processing Modes.
    Oversampling oversampling = Oversampling::x1;
};

// Each table is the output of the dyn-lab command named with it, copied as printed.
const std::vector<Table>& tables()
{
    static const std::vector<Table> all {
        { "python3 tools/dyn-lab/dynlab.py colour --character vca",
          Character::VCA,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -112.03, absent, -113.47 } },
            { 10.0, { absent, -50.75, absent, -62.96 } },
            { 20.0, { absent, -38.38, absent, -51.77 } },
            { 30.0, { absent, -38.33, absent, -46.24 } } } },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --colour 50",
          Character::VCA,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -118.05, absent, -119.49 } },
            { 10.0, { absent, -56.79, absent, -69.01 } },
            { 20.0, { absent, -44.57, absent, -57.97 } },
            { 30.0, { absent, -44.63, absent, -52.54 } } } },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet",
          Character::FET,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -68.46, absent, -73.21 } },
            { 10.0, { absent, -39.58, absent, -56.60 } },
            { 20.0, { absent, -30.73, absent, -43.45 } },
            { 30.0, { absent, -30.45, absent, -38.32 } } } },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --colour 50",
          Character::FET,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -74.48, absent, -79.23 } },
            { 10.0, { absent, -45.68, absent, -62.71 } },
            { 20.0, { absent, -37.18, absent, -49.90 } },
            { 30.0, { absent, -37.16, absent, -45.03 } } } },
        // Oversampled, ADAA's half-sample average droops the harmonics less: High Quality at each factor.
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --oversampling 2",
          Character::VCA,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -111.61, absent, -113.00 } },
            { 10.0, { absent, -50.66, absent, -62.83 } },
            { 20.0, { absent, -38.32, absent, -51.62 } },
            { 30.0, { absent, -38.28, absent, -46.11 } } },
          Oversampling::x2 },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --oversampling 4",
          Character::VCA,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -111.51, absent, -112.88 } },
            { 10.0, { absent, -50.64, absent, -62.80 } },
            { 20.0, { absent, -38.31, absent, -51.58 } },
            { 30.0, { absent, -38.27, absent, -46.07 } } },
          Oversampling::x4 },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --oversampling 8",
          Character::VCA,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -111.49, absent, -112.85 } },
            { 10.0, { absent, -50.64, absent, -62.79 } },
            { 20.0, { absent, -38.31, absent, -51.57 } },
            { 30.0, { absent, -38.26, absent, -46.07 } } },
          Oversampling::x8 },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --colour 50 --oversampling 2",
          Character::VCA,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -117.63, absent, -119.02 } },
            { 10.0, { absent, -56.71, absent, -68.87 } },
            { 20.0, { absent, -44.52, absent, -57.81 } },
            { 30.0, { absent, -44.59, absent, -52.41 } } },
          Oversampling::x2 },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --colour 50 --oversampling 4",
          Character::VCA,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -117.53, absent, -118.90 } },
            { 10.0, { absent, -56.68, absent, -68.84 } },
            { 20.0, { absent, -44.51, absent, -57.78 } },
            { 30.0, { absent, -44.58, absent, -52.38 } } },
          Oversampling::x4 },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --colour 50 --oversampling 8",
          Character::VCA,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -117.51, absent, -118.87 } },
            { 10.0, { absent, -56.68, absent, -68.83 } },
            { 20.0, { absent, -44.50, absent, -57.77 } },
            { 30.0, { absent, -44.57, absent, -52.37 } } },
          Oversampling::x8 },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --oversampling 2",
          Character::FET,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -68.31, absent, -73.00 } },
            { 10.0, { absent, -39.50, absent, -56.49 } },
            { 20.0, { absent, -30.67, absent, -43.30 } },
            { 30.0, { absent, -30.39, absent, -38.18 } } },
          Oversampling::x2 },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --oversampling 4",
          Character::FET,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -68.27, absent, -72.95 } },
            { 10.0, { absent, -39.48, absent, -56.47 } },
            { 20.0, { absent, -30.66, absent, -43.26 } },
            { 30.0, { absent, -30.38, absent, -38.15 } } },
          Oversampling::x4 },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --oversampling 8",
          Character::FET,
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -68.26, absent, -72.94 } },
            { 10.0, { absent, -39.48, absent, -56.46 } },
            { 20.0, { absent, -30.65, absent, -43.25 } },
            { 30.0, { absent, -30.38, absent, -38.14 } } },
          Oversampling::x8 },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --colour 50 --oversampling 2",
          Character::FET,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -74.33, absent, -79.02 } },
            { 10.0, { absent, -45.61, absent, -62.60 } },
            { 20.0, { absent, -37.12, absent, -49.75 } },
            { 30.0, { absent, -37.11, absent, -44.90 } } },
          Oversampling::x2 },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --colour 50 --oversampling 4",
          Character::FET,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -74.29, absent, -78.97 } },
            { 10.0, { absent, -45.59, absent, -62.58 } },
            { 20.0, { absent, -37.11, absent, -49.71 } },
            { 30.0, { absent, -37.10, absent, -44.87 } } },
          Oversampling::x4 },
        { "python3 tools/dyn-lab/dynlab.py colour --character fet --colour 50 --oversampling 8",
          Character::FET,
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -74.29, absent, -78.96 } },
            { 10.0, { absent, -45.59, absent, -62.57 } },
            { 20.0, { absent, -37.11, absent, -49.70 } },
            { 30.0, { absent, -37.10, absent, -44.86 } } },
          Oversampling::x8 },
    };
    return all;
}

// dyn-lab's COLOUR_HARMONIC_TOLERANCE_DB, and the level under which a harmonic is float's rounding, not compared.
constexpr double harmonicToleranceDb = 0.1;
constexpr double comparedOverDb = -100.0;

} // namespace

TEST_CASE ("On VCA and FET, Colour follows the Gain Reduction: the harmonics match dyn-lab's colour within its tolerance, in each Processing Mode and at every Oversampling", "[sweep]")
{
    const auto& table = GENERATE (from_range (tables()));
    const auto& row = GENERATE_REF (from_range (table.rows));
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (table.command, row.reductionDb, static_cast<int> (mode));
    // An oversampled table is High Quality's alone.
    if (table.oversampling != Oversampling::x1 && mode == ProcessingMode::ZeroLatency)
        return;
    const Settings settings { .character = table.character, .colour = table.colour, .processingMode = mode, .oversampling = table.oversampling };
    const auto spectrum = test::amplitudes (colouredTone (settings, test::levelForReductionDb (row.reductionDb)));
    const auto bin = static_cast<std::size_t> (test::toneBin (1000.0, sampleRate));
    for (std::size_t h = 0; h < row.harmonicsDb.size(); ++h)
    {
        CAPTURE (h + 2);
        const double levelDb = 20.0 * std::log10 (spectrum[(h + 2) * bin] / spectrum[bin]);
        if (row.harmonicsDb[h] > comparedOverDb)
            CHECK (std::abs (levelDb - row.harmonicsDb[h]) <= harmonicToleranceDb);
        else
            CHECK (levelDb < comparedOverDb);
    }
}

TEST_CASE ("At Colour 0% VCA adds no distortion: doubling the main input doubles the output bit for bit, under compression")
{
    // 10 dB of Gain Reduction, set by the Sidechain alone, so it is the same for both inputs.
    const double levelDb = test::levelForReductionDb (10.0);
    const auto once = colouredTone ({ .character = Character::VCA, .colour = 0.0 }, levelDb);
    const auto twice = colouredTone ({ .character = Character::VCA, .colour = 0.0 }, levelDb, 2.0);
    const auto tone = test::binCentredTone (test::toneBin (1000.0, sampleRate), levelDb, lengths);
    bool exact = true, compressed = false;
    for (std::size_t i = 0; i < once.size(); ++i)
    {
        exact = exact && twice[i] == 2.0f * once[i];
        compressed = compressed || std::abs (once[i]) < 0.5f * std::abs (tone[i]);
    }
    CHECK (exact);
    CHECK (compressed);

    // At Colour 100% the residual isn't linear in the input, so doubling it doesn't double the output.
    const auto colouredOnce = colouredTone ({ .character = Character::VCA }, levelDb);
    const auto colouredTwice = colouredTone ({ .character = Character::VCA }, levelDb, 2.0);
    bool doubled = true;
    for (std::size_t i = 0; i < colouredOnce.size(); ++i)
        doubled = doubled && colouredTwice[i] == 2.0f * colouredOnce[i];
    CHECK_FALSE (doubled);
}

TEST_CASE ("With no Gain Reduction, VCA at Colour 100% passes the input bit for bit")
{
    // 12 dB under the Knee: no Gain Reduction, so Colour's weight is exactly 0.
    const test::Channels input { test::binCentredTone (test::toneBin (1000.0, sampleRate), test::levelForReductionDb (0.0), lengths) };
    CHECK (test::played ({ .character = Character::VCA }, sampleRate, input) == input);
}

TEST_CASE ("Clean has no Colour: its output is the same at every Colour, and Opto plays as Clean")
{
    const double rate = GENERATE (from_range (test::sampleRates()));
    const auto& cut = GENERATE (from_range (test::blockCuts()));
    CAPTURE (rate, cut);
    // Noise under deep compression: Threshold -40 dB.
    const auto input = test::noise (2, static_cast<int> (0.1 * rate));
    const auto clean = test::played ({ .thresholdDb = -40.0, .colour = 100.0 }, rate, input, cut);
    CHECK (clean != input);
    CHECK (test::played ({ .thresholdDb = -40.0, .colour = 0.0 }, rate, input, cut) == clean);
    CHECK (test::played ({ .thresholdDb = -40.0, .colour = 37.0 }, rate, input, cut) == clean);
    CHECK (test::played ({ .character = Character::Opto, .thresholdDb = -40.0 }, rate, input, cut) == clean);
}

TEST_CASE ("The detector never hears Colour: VCA's and FET's Gain Reduction is the same at Colour 0% and 100%, in each Processing Mode")
{
    // FET's loop too: it hears the detection signal times the gain, never the coloured output.
    const auto character = GENERATE (Character::VCA, Character::FET);
    // Zero Latency, and High Quality with Colour oversampled.
    const auto mode = GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality);
    CAPTURE (static_cast<int> (character), static_cast<int> (mode));
    // Noise under deep compression, where Colour is at its most.
    const auto input = test::noise (2, 24000);
    const auto at = [&] (double colour) {
        return Settings { .character = character, .thresholdDb = -40.0, .colour = colour, .processingMode = mode, .oversampling = Oversampling::x4 };
    };
    const auto off = test::levelHistory (at (0.0), sampleRate, input);
    const auto full = test::levelHistory (at (100.0), sampleRate, input);
    REQUIRE (off.size() == full.size());
    bool same = true;
    for (std::size_t r = 0; r < off.size(); ++r)
        same = same && off[r].gainReductionDb == full[r].gainReductionDb && off[r].detectionLevelDb == full[r].detectionLevelDb;
    CHECK (same);
    CHECK (test::played (at (0.0), sampleRate, input) != test::played (at (100.0), sampleRate, input));
}

TEST_CASE ("Under Stereo Placement Mid or Side, Colour shapes only the part compressed and the other passes, in Zero Latency and oversampled", "[sweep]")
{
    const bool midLoud = GENERATE (true, false);
    // Zero Latency, and High Quality oversampled at 2x and 8x.
    const auto oversampling = GENERATE (std::optional<Oversampling> {}, std::optional { Oversampling::x2 }, std::optional { Oversampling::x8 });
    CAPTURE (midLoud, oversampling ? factorOf (*oversampling) : 0);
    // The compressed part: the tone at 20 dB of Gain Reduction, held steady by a mono Sidechain, which drives every
    // Placement; the other part, quiet noise.
    const int bin = test::toneBin (1000.0, sampleRate);
    const double levelDb = test::levelForReductionDb (20.0);
    const auto loud = test::binCentredTone (bin, levelDb, lengths);
    auto quiet = test::noise (1, static_cast<int> (loud.size())).front();
    for (auto& sample : quiet)
        sample *= 0.01f;
    test::Channels input (2);
    for (std::size_t i = 0; i < loud.size(); ++i)
    {
        const float mid = midLoud ? loud[i] : quiet[i], side = midLoud ? quiet[i] : loud[i];
        input[0].push_back (plugkit::decodeLeft (mid, side));
        input[1].push_back (plugkit::decodeRight (mid, side));
    }
    const auto sidechain = test::steadyLevel (1, static_cast<int> (loud.size()), levelDb - test::sineCalibrationDb);
    const Settings settings { .character = Character::VCA,
                              .detectionSource = DetectionSource::External,
                              .stereoPlacement = midLoud ? StereoPlacement::Mid : StereoPlacement::Side,
                              .processingMode = oversampling ? ProcessingMode::HighQuality : ProcessingMode::ZeroLatency,
                              .oversampling = oversampling.value_or (Oversampling::x1) };
    // High Quality's output, brought back in line with its input.
    const int latency = oversampling ? latencySamples (ProcessingMode::HighQuality, sampleRate) : 0;
    auto output = test::played (settings, sampleRate, input, { 512 }, &sidechain);
    for (auto& channel : output)
        channel.erase (channel.begin(), channel.begin() + latency);
    std::vector<float> loudOut;
    double largestChange = 0.0;
    for (std::size_t i = 0; i + static_cast<std::size_t> (latency) < loud.size(); ++i)
    {
        const auto encode = [midLoud] (const test::Channels& stereo, std::size_t n, bool loudPart) {
            return loudPart == midLoud ? plugkit::encodeMid (stereo[0][n], stereo[1][n]) : plugkit::encodeSide (stereo[0][n], stereo[1][n]);
        };
        loudOut.push_back (encode (output, i, true));
        largestChange = std::max (largestChange, std::abs (static_cast<double> (encode (output, i, false)) - encode (input, i, false)));
    }
    // The quiet part passes, but for the rounding of Left and Right, which carry the loud part, under 2.7 (+8.67 dB):
    // a float step there is 2^-22. Colour on Left and Right each, rather than on the part, would move it by its whole
    // change.
    CHECK (largestChange <= 0x1p-21);
    // The compressed part has Colour's 3rd harmonic, as dyn-lab's colour gives it at 20 dB of Gain Reduction (within
    // the tolerance at every factor: -38.32 dB at 2x, -38.30 dB at 8x).
    const auto spectrum = test::amplitudes (loudOut);
    const auto b = static_cast<std::size_t> (bin);
    CHECK (std::abs (20.0 * std::log10 (spectrum[3 * b] / spectrum[b]) - -38.38) <= harmonicToleranceDb);
}
