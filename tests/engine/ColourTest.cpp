#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/Placement.h>

#include <array>
#include <cmath>
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
    double colour;
    std::vector<Row> rows;
};

// Each table is the output of the dyn-lab command named with it, copied as printed.
const std::vector<Table>& tables()
{
    static const std::vector<Table> all {
        { "python3 tools/dyn-lab/dynlab.py colour --character vca",
          100.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -112.03, absent, -113.47 } },
            { 10.0, { absent, -50.75, absent, -62.96 } },
            { 20.0, { absent, -38.38, absent, -51.77 } },
            { 30.0, { absent, -38.33, absent, -46.24 } } } },
        { "python3 tools/dyn-lab/dynlab.py colour --character vca --colour 50",
          50.0,
          { { 0.0, { absent, absent, absent, absent } },
            { 3.0, { absent, absent, absent, absent } },
            { 6.0, { absent, -118.05, absent, -119.49 } },
            { 10.0, { absent, -56.79, absent, -69.01 } },
            { 20.0, { absent, -44.57, absent, -57.97 } },
            { 30.0, { absent, -44.63, absent, -52.54 } } } },
    };
    return all;
}

// dyn-lab's COLOUR_HARMONIC_TOLERANCE_DB, and the level under which a harmonic is float's rounding, not compared.
constexpr double harmonicToleranceDb = 0.1;
constexpr double comparedOverDb = -100.0;

} // namespace

TEST_CASE ("On VCA, Colour follows the Gain Reduction: the harmonics match dyn-lab's colour within its tolerance")
{
    const auto& table = GENERATE (from_range (tables()));
    const auto& row = GENERATE_REF (from_range (table.rows));
    CAPTURE (table.command, row.reductionDb);
    const auto spectrum = test::amplitudes (colouredTone ({ .character = Character::VCA, .colour = table.colour }, test::levelForReductionDb (row.reductionDb)));
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

TEST_CASE ("Clean has no Colour: its output is the same at every Colour, and FET and Opto play as Clean")
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
    CHECK (test::played ({ .character = Character::FET, .thresholdDb = -40.0 }, rate, input, cut) == clean);
    CHECK (test::played ({ .character = Character::Opto, .thresholdDb = -40.0 }, rate, input, cut) == clean);
}

TEST_CASE ("The detector never hears Colour: VCA's Gain Reduction is the same at Colour 0% and 100%")
{
    // Noise under deep compression, where Colour is at its most.
    const auto input = test::noise (2, 24000);
    const auto off = test::levelHistory ({ .character = Character::VCA, .thresholdDb = -40.0, .colour = 0.0 }, sampleRate, input);
    const auto full = test::levelHistory ({ .character = Character::VCA, .thresholdDb = -40.0, .colour = 100.0 }, sampleRate, input);
    REQUIRE (off.size() == full.size());
    bool same = true;
    for (std::size_t r = 0; r < off.size(); ++r)
        same = same && off[r].gainReductionDb == full[r].gainReductionDb && off[r].detectionLevelDb == full[r].detectionLevelDb;
    CHECK (same);
    CHECK (test::played ({ .character = Character::VCA, .thresholdDb = -40.0, .colour = 0.0 }, sampleRate, input)
           != test::played ({ .character = Character::VCA, .thresholdDb = -40.0, .colour = 100.0 }, sampleRate, input));
}

TEST_CASE ("Under Stereo Placement Mid or Side, Colour shapes only the part compressed and the other passes")
{
    const bool midLoud = GENERATE (true, false);
    CAPTURE (midLoud);
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
                              .stereoPlacement = midLoud ? StereoPlacement::Mid : StereoPlacement::Side };
    const auto output = test::played (settings, sampleRate, input, { 512 }, &sidechain);
    std::vector<float> loudOut;
    double largestChange = 0.0;
    for (std::size_t i = 0; i < loud.size(); ++i)
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
    // The compressed part has Colour's 3rd harmonic, as dyn-lab's colour gives it at 20 dB of Gain Reduction.
    const auto spectrum = test::amplitudes (loudOut);
    const auto b = static_cast<std::size_t> (bin);
    CHECK (std::abs (20.0 * std::log10 (spectrum[3 * b] / spectrum[b]) - -38.38) <= harmonicToleranceDb);
}
