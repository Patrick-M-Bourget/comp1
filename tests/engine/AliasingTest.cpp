#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

// dyn-lab's ALIASING_TOLERANCE (tools/dyn-lab/USAGE.md, "aliasing"): the worst alias is at most ceilingDb relative to
// the fundamental at every reference condition, and at the hardest at least improvementDb under the naive shaper's.
constexpr double ceilingDb = -24.0;
constexpr double improvementDb = 15.0;
// How near the lab's figure the Engine's worst alias falls. The Engine's input and output are float, the lab's double:
// at the lowest aliases, -100 dB, float's rounding is about 50 dB under them.
constexpr double labWithinDb = 0.5;

// One reference condition: a bin-centred tone at 0 dBFS through Saturation at a Drive, and the worst alias dyn-lab
// gives for it with ADAA, and for the naive shaper where the improvement is checked.
struct Condition
{
    double sampleRate;
    double frequency; // nominal: the tone is the bin-centred one nearest it
    double driveDb;
    double labDb;
    std::optional<double> naiveDb; // the hardest condition's
};

// The rows `python3 tools/dyn-lab/dynlab.py aliasing` prints, copied as printed. Oversampling (#10) and Colour (#19)
// add tables of their own.
const std::vector<Condition>& zeroLatency()
{
    static const std::vector<Condition> all {
        { 44100.0, 1000.0, 6.0, -100.35, {} },    { 44100.0, 1000.0, 12.0, -79.40, {} },
        { 44100.0, 1000.0, 18.0, -57.17, {} },    { 44100.0, 1000.0, 24.0, -53.68, {} },
        { 44100.0, 3000.0, 6.0, -65.93, {} },     { 44100.0, 3000.0, 12.0, -47.99, {} },
        { 44100.0, 3000.0, 18.0, -38.46, {} },    { 44100.0, 3000.0, 24.0, -32.44, {} },
        { 44100.0, 5000.0, 6.0, -45.88, {} },     { 44100.0, 5000.0, 12.0, -36.35, {} },
        { 44100.0, 5000.0, 18.0, -26.25, {} },    { 44100.0, 5000.0, 24.0, -27.84, {} },
        { 44100.0, 10000.0, 6.0, -50.86, {} },    { 44100.0, 10000.0, 12.0, -31.34, {} },
        { 44100.0, 10000.0, 18.0, -28.72, {} },   { 44100.0, 10000.0, 24.0, -32.57, -9.79 },
        { 48000.0, 1000.0, 6.0, -102.27, {} },    { 48000.0, 1000.0, 12.0, -87.24, {} },
        { 48000.0, 1000.0, 18.0, -61.78, {} },    { 48000.0, 1000.0, 24.0, -60.80, {} },
        { 48000.0, 3000.0, 6.0, -64.78, {} },     { 48000.0, 3000.0, 12.0, -46.79, {} },
        { 48000.0, 3000.0, 18.0, -37.01, {} },    { 48000.0, 3000.0, 24.0, -30.85, {} },
        { 48000.0, 5000.0, 6.0, -44.59, {} },     { 48000.0, 5000.0, 12.0, -34.74, {} },
        { 48000.0, 5000.0, 18.0, -24.80, {} },    { 48000.0, 5000.0, 24.0, -26.24, {} },
        { 48000.0, 10000.0, 6.0, -47.66, {} },    { 48000.0, 10000.0, 12.0, -27.12, {} },
        { 48000.0, 10000.0, 18.0, -25.53, {} },   { 48000.0, 10000.0, 24.0, -29.54, -9.79 },
    };
    return all;
}

// The tone's spectrum through the Engine, at Ratio 1:1 so only Saturation shapes it, at either Saturation Position:
// two lengths of it, the second measured, as dyn-lab measures it.
std::vector<double> saturatedSpectrum (double sampleRate, int bin, double driveDb, SaturationPosition position)
{
    test::Channels audio { test::binCentredTone (bin, 0.0) };
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings ({ .ratio = 1.0, .saturation = true, .driveDb = driveDb, .saturationPosition = position });
    test::processInBlocks (engine, audio, { 512 });
    return test::amplitudes (audio.front());
}

} // namespace

TEST_CASE ("In Zero Latency, the worst alias at each of dyn-lab's reference conditions is within its tolerance")
{
    const auto& condition = GENERATE (from_range (zeroLatency()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (condition.sampleRate, condition.frequency, condition.driveDb, static_cast<int> (position));
    const int bin = test::toneBin (condition.frequency, condition.sampleRate);
    const double worst = test::worstAliasDb (saturatedSpectrum (condition.sampleRate, bin, condition.driveDb, position), bin);
    CHECK (worst <= ceilingDb);
    CHECK (std::abs (worst - condition.labDb) <= labWithinDb);
    if (condition.naiveDb)
        CHECK (*condition.naiveDb - worst >= improvementDb);
}

namespace
{

// dyn-lab's HARMONIC_TOLERANCE_DB: ADAA's in-band harmonics are within this of the ideal reference's.
constexpr double harmonicToleranceDb = 4.0;

// An in-band harmonic of a reference tone at 0 dBFS through Saturation at a Drive, and the ideal reference's peak
// amplitude in dB for it.
struct Harmonic
{
    double sampleRate;
    double frequency; // the tone's, nominal
    double driveDb;
    int harmonic;
    double idealDb;
};

// The rows `python3 tools/dyn-lab/dynlab.py aliasing` prints under its in-band harmonics, copied as printed. The ideal
// reference depends only on the Drive and the harmonic, so each rate's rows give the same ideal figures.
std::vector<Harmonic> harmonics()
{
    // ideal[d][h]: Drive 6, 12, 18, 24 dB; the 3rd and 5th harmonics (the even ones are absent: the shape is odd).
    const double drives[] = { 6.0, 12.0, 18.0, 24.0 };
    const double third[] = { -27.84, -18.25, -20.55, -25.70 };
    const double fifth[] = { -39.74, -32.12, -27.03, -30.63 };
    std::vector<Harmonic> rows;
    for (const double sampleRate : { 44100.0, 48000.0 })
        for (const double frequency : { 1000.0, 3000.0, 5000.0 })
            for (std::size_t d = 0; d < 4; ++d)
            {
                rows.push_back ({ sampleRate, frequency, drives[d], 3, third[d] });
                // The 5th harmonic of 5 kHz is past Nyquist.
                if (frequency < 5000.0)
                    rows.push_back ({ sampleRate, frequency, drives[d], 5, fifth[d] });
            }
    return rows;
}

} // namespace

TEST_CASE ("In Zero Latency, the in-band harmonics are within dyn-lab's tolerance of its ideal reference")
{
    const auto rows = harmonics();
    const auto& row = GENERATE_REF (from_range (rows));
    CAPTURE (row.sampleRate, row.frequency, row.driveDb, row.harmonic);
    const int bin = test::toneBin (row.frequency, row.sampleRate);
    const auto spectrum = saturatedSpectrum (row.sampleRate, bin, row.driveDb, SaturationPosition::After);
    const double levelDb = 20.0 * std::log10 (spectrum[static_cast<std::size_t> (row.harmonic * bin)]);
    CHECK (std::abs (levelDb - row.idealDb) <= harmonicToleranceDb);
    // The even harmonics are absent, as the shape is odd.
    CHECK (20.0 * std::log10 (spectrum[static_cast<std::size_t> (2 * bin)] / spectrum[static_cast<std::size_t> (bin)]) < -100.0);
}
