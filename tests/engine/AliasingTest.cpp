#include "Measure.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <cstddef>
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

// The rows `python3 tools/dyn-lab/dynlab.py aliasing` prints, copied as printed. Oversampling's tables and Colour's
// are below.
const std::vector<Condition>& baseRate()
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

// The tone's spectrum through the Engine, at Ratio 1:1 so only Saturation shapes it, at a Saturation Position and in a
// Processing Mode, at an Oversampling: two lengths of it, the second measured, as dyn-lab measures it. High Quality's
// latency is far under a length, so its second is steady too.
std::vector<double> saturatedSpectrum (double sampleRate,
                                       int bin,
                                       double driveDb,
                                       SaturationPosition position,
                                       ProcessingMode mode,
                                       Oversampling oversampling = Oversampling::x1)
{
    test::Channels audio { test::binCentredTone (bin, 0.0) };
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings ({ .ratio = 1.0,
                          .saturation = true,
                          .driveDb = driveDb,
                          .saturationPosition = position,
                          .processingMode = mode,
                          .oversampling = oversampling });
    test::processInBlocks (engine, audio, { 512 });
    return test::amplitudes (audio.front());
}

// Zero Latency, and High Quality at 1x, which runs Saturation at the base rate too.
ProcessingMode atBaseRate() { return GENERATE (ProcessingMode::ZeroLatency, ProcessingMode::HighQuality); }

} // namespace

TEST_CASE ("At the base rate, the worst alias at each of dyn-lab's reference conditions is within its tolerance", "[sweep]")
{
    const auto& condition = GENERATE (from_range (baseRate()));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    const auto mode = atBaseRate();
    CAPTURE (condition.sampleRate, condition.frequency, condition.driveDb, static_cast<int> (position), static_cast<int> (mode));
    const int bin = test::toneBin (condition.frequency, condition.sampleRate);
    const double worst = test::worstAliasDb (saturatedSpectrum (condition.sampleRate, bin, condition.driveDb, position, mode), bin);
    CHECK (worst <= ceilingDb);
    CHECK (std::abs (worst - condition.labDb) <= labWithinDb);
    if (condition.naiveDb)
        CHECK (*condition.naiveDb - worst >= improvementDb);
}

namespace
{

// dyn-lab's ALIASING_TOLERANCE passband_ceilings: oversampled, the worst alias below the passband edge is at most its
// factor's ceiling, and the worst anywhere no worse than ADAA's at 1x.
constexpr double passbandCeilingDb (Oversampling oversampling)
{
    return oversampling == Oversampling::x2 ? -45.0 : oversampling == Oversampling::x4 ? -60.0 : -75.0;
}
// dyn-lab's OVERSAMPLING_PASSBAND_EDGE, of Nyquist: the band below it is the one the filters keep flat.
constexpr double passbandEdge = 0.8;
// Under this the lab's figures meet the float audio's own rounding, so the Engine's are only checked against the
// ceiling there.
constexpr double comparedAboveDb = -110.0;

// One reference condition oversampled: the worst alias dyn-lab gives for it anywhere, and below the passband edge.
struct Oversampled
{
    double sampleRate;
    double frequency; // nominal
    double driveDb;
    double labDb;
    double passbandDb;
};

// The rows `python3 tools/dyn-lab/dynlab.py aliasing --oversampling N` prints, copied as printed: each factor's
// worst alias anywhere and below the passband edge (1x's column is baseRate()'s).
const std::vector<Oversampled>& oversampledRows (Oversampling oversampling)
{
    static const std::vector<Oversampled> x2 {
        { 44100.0, 1000.0, 6.0, -109.27, -148.55 }, { 44100.0, 1000.0, 12.0, -88.42, -127.78 },
        { 44100.0, 1000.0, 18.0, -66.19, -112.89 }, { 44100.0, 1000.0, 24.0, -62.23, -87.26 },
        { 44100.0, 3000.0, 6.0, -109.19, -115.82 }, { 44100.0, 3000.0, 12.0, -87.74, -104.25 },
        { 44100.0, 3000.0, 18.0, -65.31, -73.92 }, { 44100.0, 3000.0, 24.0, -62.58, -69.85 },
        { 44100.0, 5000.0, 6.0, -81.14, -92.52 }, { 44100.0, 5000.0, 12.0, -70.17, -83.54 },
        { 44100.0, 5000.0, 18.0, -60.04, -64.33 }, { 44100.0, 5000.0, 24.0, -50.53, -50.53 },
        { 44100.0, 10000.0, 6.0, -83.46, -83.95 }, { 44100.0, 10000.0, 12.0, -56.80, -67.49 },
        { 44100.0, 10000.0, 18.0, -41.10, -55.29 }, { 44100.0, 10000.0, 24.0, -39.45, -54.52 },
        { 48000.0, 1000.0, 6.0, -114.60, -150.55 }, { 48000.0, 1000.0, 12.0, -105.65, -130.18 },
        { 48000.0, 1000.0, 18.0, -69.62, -115.76 }, { 48000.0, 1000.0, 24.0, -70.74, -90.26 },
        { 48000.0, 3000.0, 6.0, -98.54, -112.68 }, { 48000.0, 3000.0, 12.0, -80.29, -98.87 },
        { 48000.0, 3000.0, 18.0, -69.47, -80.64 }, { 48000.0, 3000.0, 24.0, -62.01, -71.18 },
        { 48000.0, 5000.0, 6.0, -52.52, -107.11 }, { 48000.0, 5000.0, 12.0, -41.48, -81.44 },
        { 48000.0, 5000.0, 18.0, -31.29, -65.29 }, { 48000.0, 5000.0, 24.0, -31.01, -56.35 },
        { 48000.0, 10000.0, 6.0, -80.13, -80.13 }, { 48000.0, 10000.0, 12.0, -61.76, -61.76 },
        { 48000.0, 10000.0, 18.0, -57.00, -57.00 }, { 48000.0, 10000.0, 24.0, -48.07, -48.07 },
    };
    static const std::vector<Oversampled> x4 {
        { 44100.0, 1000.0, 6.0, -108.45, -178.84 }, { 44100.0, 1000.0, 12.0, -87.62, -166.26 },
        { 44100.0, 1000.0, 18.0, -65.39, -142.05 }, { 44100.0, 1000.0, 24.0, -61.30, -127.64 },
        { 44100.0, 3000.0, 6.0, -148.51, -148.51 }, { 44100.0, 3000.0, 12.0, -123.51, -123.51 },
        { 44100.0, 3000.0, 18.0, -110.67, -111.00 }, { 44100.0, 3000.0, 24.0, -88.48, -88.48 },
        { 44100.0, 5000.0, 6.0, -80.06, -128.37 }, { 44100.0, 5000.0, 12.0, -68.74, -110.18 },
        { 44100.0, 5000.0, 18.0, -58.25, -92.83 }, { 44100.0, 5000.0, 24.0, -56.77, -76.71 },
        { 44100.0, 10000.0, 6.0, -117.44, -117.44 }, { 44100.0, 10000.0, 12.0, -85.70, -85.70 },
        { 44100.0, 10000.0, 18.0, -70.01, -70.01 }, { 44100.0, 10000.0, 24.0, -62.80, -62.80 },
        { 48000.0, 1000.0, 6.0, -113.93, -184.28 }, { 48000.0, 1000.0, 12.0, -104.74, -166.90 },
        { 48000.0, 1000.0, 18.0, -68.82, -152.78 }, { 48000.0, 1000.0, 24.0, -69.81, -126.88 },
        { 48000.0, 3000.0, 6.0, -97.65, -152.35 }, { 48000.0, 3000.0, 12.0, -79.33, -131.61 },
        { 48000.0, 3000.0, 18.0, -68.23, -109.08 }, { 48000.0, 3000.0, 24.0, -60.12, -97.05 },
        { 48000.0, 5000.0, 6.0, -51.61, -135.50 }, { 48000.0, 5000.0, 12.0, -40.28, -113.72 },
        { 48000.0, 5000.0, 18.0, -29.76, -89.06 }, { 48000.0, 5000.0, 24.0, -28.17, -73.50 },
        { 48000.0, 10000.0, 6.0, -107.79, -108.51 }, { 48000.0, 10000.0, 12.0, -82.20, -87.84 },
        { 48000.0, 10000.0, 18.0, -66.04, -70.62 }, { 48000.0, 10000.0, 24.0, -57.08, -65.68 },
    };
    static const std::vector<Oversampled> x8 {
        { 44100.0, 1000.0, 6.0, -108.26, -199.75 }, { 44100.0, 1000.0, 12.0, -87.43, -182.09 },
        { 44100.0, 1000.0, 18.0, -65.19, -171.17 }, { 44100.0, 1000.0, 24.0, -61.07, -156.81 },
        { 44100.0, 3000.0, 6.0, -159.13, -159.13 }, { 44100.0, 3000.0, 12.0, -140.61, -140.61 },
        { 44100.0, 3000.0, 18.0, -128.55, -128.55 }, { 44100.0, 3000.0, 24.0, -118.80, -120.71 },
        { 44100.0, 5000.0, 6.0, -79.79, -157.44 }, { 44100.0, 5000.0, 12.0, -68.39, -136.26 },
        { 44100.0, 5000.0, 18.0, -57.78, -118.70 }, { 44100.0, 5000.0, 24.0, -55.71, -100.69 },
        { 44100.0, 10000.0, 6.0, -138.41, -139.89 }, { 44100.0, 10000.0, 12.0, -120.21, -124.13 },
        { 44100.0, 10000.0, 18.0, -95.24, -95.24 }, { 44100.0, 10000.0, 24.0, -79.86, -79.86 },
        { 48000.0, 1000.0, 6.0, -113.76, -199.33 }, { 48000.0, 1000.0, 12.0, -104.52, -182.15 },
        { 48000.0, 1000.0, 18.0, -68.63, -170.68 }, { 48000.0, 1000.0, 24.0, -69.59, -156.66 },
        { 48000.0, 3000.0, 6.0, -97.44, -173.39 }, { 48000.0, 3000.0, 12.0, -79.10, -155.38 },
        { 48000.0, 3000.0, 18.0, -67.93, -144.61 }, { 48000.0, 3000.0, 24.0, -59.61, -122.63 },
        { 48000.0, 5000.0, 6.0, -51.39, -161.41 }, { 48000.0, 5000.0, 12.0, -39.98, -147.59 },
        { 48000.0, 5000.0, 18.0, -29.37, -134.22 }, { 48000.0, 5000.0, 24.0, -27.26, -100.56 },
        { 48000.0, 10000.0, 6.0, -126.26, -126.26 }, { 48000.0, 10000.0, 12.0, -117.01, -117.01 },
        { 48000.0, 10000.0, 18.0, -97.85, -97.85 }, { 48000.0, 10000.0, 24.0, -82.15, -82.15 },
    };
    return oversampling == Oversampling::x2 ? x2 : oversampling == Oversampling::x4 ? x4 : x8;
}

// The same condition's row at the base rate.
const Condition& atBase (const Oversampled& row)
{
    for (const auto& condition : baseRate())
        if (condition.sampleRate == row.sampleRate && condition.frequency == row.frequency && condition.driveDb == row.driveDb)
            return condition;
    FAIL ("no base-rate row for the condition");
    return baseRate().front();
}

struct OversampledCondition
{
    Oversampling oversampling;
    Oversampled row;
};

std::vector<OversampledCondition> oversampledConditions()
{
    std::vector<OversampledCondition> all;
    for (const auto oversampling : { Oversampling::x2, Oversampling::x4, Oversampling::x8 })
        for (const auto& row : oversampledRows (oversampling))
            all.push_back ({ oversampling, row });
    return all;
}

} // namespace

TEST_CASE ("Oversampled, the worst alias at each of dyn-lab's reference conditions is within its factor's tolerance and no worse than at 1x", "[sweep]")
{
    const auto conditions = oversampledConditions();
    const auto& [oversampling, row] = GENERATE_REF (from_range (conditions));
    // One position: both run the same oversampled section, and the base-rate rows check both.
    const auto position = SaturationPosition::After;
    CAPTURE (factorOf (oversampling), row.sampleRate, row.frequency, row.driveDb);
    const int bin = test::toneBin (row.frequency, row.sampleRate);
    const auto spectrum = saturatedSpectrum (row.sampleRate, bin, row.driveDb, position, ProcessingMode::HighQuality, oversampling);
    const double worst = test::worstAliasDb (spectrum, bin);
    const auto top = static_cast<std::size_t> (passbandEdge * test::spectrumLength / 2);
    const double passband = test::worstAliasDb ({ spectrum.begin(), spectrum.begin() + static_cast<std::ptrdiff_t> (top + 1) }, bin);
    CHECK (passband <= passbandCeilingDb (oversampling));
    CHECK (worst <= atBase (row).labDb);
    if (row.labDb > comparedAboveDb)
        CHECK (std::abs (worst - row.labDb) <= labWithinDb);
    if (row.passbandDb > comparedAboveDb)
        CHECK (std::abs (passband - row.passbandDb) <= labWithinDb);
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

TEST_CASE ("At the base rate, the in-band harmonics are within dyn-lab's tolerance of its ideal reference", "[sweep]")
{
    const auto rows = harmonics();
    const auto& row = GENERATE_REF (from_range (rows));
    const auto mode = atBaseRate();
    CAPTURE (row.sampleRate, row.frequency, row.driveDb, row.harmonic, static_cast<int> (mode));
    const int bin = test::toneBin (row.frequency, row.sampleRate);
    const auto spectrum = saturatedSpectrum (row.sampleRate, bin, row.driveDb, SaturationPosition::After, mode);
    const double levelDb = 20.0 * std::log10 (spectrum[static_cast<std::size_t> (row.harmonic * bin)]);
    CHECK (std::abs (levelDb - row.idealDb) <= harmonicToleranceDb);
    // The even harmonics are absent, as the shape is odd.
    CHECK (20.0 * std::log10 (spectrum[static_cast<std::size_t> (2 * bin)] / spectrum[static_cast<std::size_t> (bin)]) < -100.0);
}

namespace
{

// One of dyn-lab's Colour conditions: a bin-centred tone through a Character at Colour 100%, at the level whose Gain
// Reduction it is, that Gain Reduction held steady, and the worst alias dyn-lab gives for it with ADAA.
struct ColourCondition
{
    double sampleRate;
    double frequency; // nominal: the tone is the bin-centred one nearest it
    double reductionDb;
    double labDb;
    Character character = Character::VCA;
};

// The aliasing rows `python3 tools/dyn-lab/dynlab.py colour --character vca`, `--character fet` and `--character opto`
// print, copied as printed. Only the ceiling is checked: the improvement over the naive shaper doesn't hold for Colour
// at 20 and 30 dB of Gain Reduction (tools/dyn-lab/USAGE.md, "colour").
const std::vector<ColourCondition>& colourConditions()
{
    static const std::vector<ColourCondition> all {
        { 44100.0, 1000.0, 10.0, -122.47 }, { 44100.0, 1000.0, 20.0, -101.65 }, { 44100.0, 1000.0, 30.0, -99.94 },
        { 44100.0, 3000.0, 10.0, -89.66 },  { 44100.0, 3000.0, 20.0, -72.30 },  { 44100.0, 3000.0, 30.0, -74.29 },
        { 44100.0, 5000.0, 10.0, -69.31 },  { 44100.0, 5000.0, 20.0, -59.05 },  { 44100.0, 5000.0, 30.0, -52.28 },
        { 44100.0, 10000.0, 10.0, -74.05 }, { 44100.0, 10000.0, 20.0, -52.87 }, { 44100.0, 10000.0, 30.0, -50.32 },
        { 48000.0, 1000.0, 10.0, -125.93 }, { 48000.0, 1000.0, 20.0, -112.54 }, { 48000.0, 1000.0, 30.0, -98.77 },
        { 48000.0, 3000.0, 10.0, -88.55 },  { 48000.0, 3000.0, 20.0, -71.18 },  { 48000.0, 3000.0, 30.0, -72.84 },
        { 48000.0, 5000.0, 10.0, -68.06 },  { 48000.0, 5000.0, 20.0, -57.61 },  { 48000.0, 5000.0, 30.0, -51.12 },
        { 48000.0, 10000.0, 10.0, -70.40 }, { 48000.0, 10000.0, 20.0, -49.00 }, { 48000.0, 10000.0, 30.0, -47.36 },
        // `python3 tools/dyn-lab/dynlab.py colour --character fet`: FET's steeper Colour aliases more, still under the
        // ceiling.
        { 44100.0, 1000.0, 10.0, -112.08, Character::FET }, { 44100.0, 1000.0, 20.0, -91.97, Character::FET },
        { 44100.0, 1000.0, 30.0, -92.21, Character::FET },  { 44100.0, 3000.0, 10.0, -88.30, Character::FET },
        { 44100.0, 3000.0, 20.0, -65.11, Character::FET },  { 44100.0, 3000.0, 30.0, -66.09, Character::FET },
        { 44100.0, 5000.0, 10.0, -61.62, Character::FET },  { 44100.0, 5000.0, 20.0, -50.64, Character::FET },
        { 44100.0, 5000.0, 30.0, -44.48, Character::FET },  { 44100.0, 10000.0, 10.0, -65.16, Character::FET },
        { 44100.0, 10000.0, 20.0, -45.22, Character::FET }, { 44100.0, 10000.0, 30.0, -42.84, Character::FET },
        { 48000.0, 1000.0, 10.0, -111.10, Character::FET }, { 48000.0, 1000.0, 20.0, -100.28, Character::FET },
        { 48000.0, 1000.0, 30.0, -91.03, Character::FET },  { 48000.0, 3000.0, 10.0, -86.02, Character::FET },
        { 48000.0, 3000.0, 20.0, -64.01, Character::FET },  { 48000.0, 3000.0, 30.0, -64.64, Character::FET },
        { 48000.0, 5000.0, 10.0, -60.64, Character::FET },  { 48000.0, 5000.0, 20.0, -49.23, Character::FET },
        { 48000.0, 5000.0, 30.0, -43.30, Character::FET },  { 48000.0, 10000.0, 10.0, -56.08, Character::FET },
        { 48000.0, 10000.0, 20.0, -41.41, Character::FET }, { 48000.0, 10000.0, 30.0, -39.84, Character::FET },
        // `python3 tools/dyn-lab/dynlab.py colour --character opto`: Opto's gentler Colour aliases least.
        { 44100.0, 1000.0, 10.0, -125.81, Character::Opto }, { 44100.0, 1000.0, 20.0, -105.09, Character::Opto },
        { 44100.0, 1000.0, 30.0, -103.45, Character::Opto }, { 44100.0, 3000.0, 10.0, -93.00, Character::Opto },
        { 44100.0, 3000.0, 20.0, -75.73, Character::Opto }, { 44100.0, 3000.0, 30.0, -77.79, Character::Opto },
        { 44100.0, 5000.0, 10.0, -72.65, Character::Opto }, { 44100.0, 5000.0, 20.0, -62.47, Character::Opto },
        { 44100.0, 5000.0, 30.0, -55.76, Character::Opto }, { 44100.0, 10000.0, 10.0, -77.38, Character::Opto },
        { 44100.0, 10000.0, 20.0, -56.25, Character::Opto }, { 44100.0, 10000.0, 30.0, -53.74, Character::Opto },
        { 48000.0, 1000.0, 10.0, -129.27, Character::Opto }, { 48000.0, 1000.0, 20.0, -115.98, Character::Opto },
        { 48000.0, 1000.0, 30.0, -102.28, Character::Opto }, { 48000.0, 3000.0, 10.0, -91.88, Character::Opto },
        { 48000.0, 3000.0, 20.0, -74.61, Character::Opto }, { 48000.0, 3000.0, 30.0, -76.34, Character::Opto },
        { 48000.0, 5000.0, 10.0, -71.40, Character::Opto }, { 48000.0, 5000.0, 20.0, -61.03, Character::Opto },
        { 48000.0, 5000.0, 30.0, -54.61, Character::Opto }, { 48000.0, 10000.0, 10.0, -73.73, Character::Opto },
        { 48000.0, 10000.0, 20.0, -52.38, Character::Opto }, { 48000.0, 10000.0, 30.0, -50.79, Character::Opto },
    };
    return all;
}

// Under this the float input and output's rounding is too near the alias for the Engine's figure to be compared with
// the lab's; it is still checked against the ceiling.
constexpr double colourComparedOverDb = -100.0;

} // namespace

TEST_CASE ("At the base rate, VCA's, FET's and Opto's worst alias at Colour 100% at each of dyn-lab's Colour conditions is under the ceiling", "[sweep]")
{
    const auto& condition = GENERATE (from_range (colourConditions()));
    const auto mode = atBaseRate();
    CAPTURE (condition.sampleRate, condition.frequency, condition.reductionDb, static_cast<int> (condition.character), static_cast<int> (mode));
    const int bin = test::toneBin (condition.frequency, condition.sampleRate);
    const auto output = test::steadilyCompressedTone ({ .character = condition.character, .processingMode = mode, .oversampling = Oversampling::x1 },
                                                      condition.sampleRate,
                                                      bin,
                                                      test::levelForReductionDb (condition.reductionDb),
                                                      8);
    const double worst = test::worstAliasDb (test::amplitudes (output), bin);
    CHECK (worst <= ceilingDb);
    if (condition.labDb > colourComparedOverDb)
        CHECK (std::abs (worst - condition.labDb) <= labWithinDb);
}

namespace
{

// One of dyn-lab's Colour conditions oversampled: the worst alias dyn-lab gives for it anywhere, and below the
// passband edge.
struct OversampledColour
{
    double sampleRate;
    double frequency; // nominal
    double reductionDb;
    double labDb;
    double passbandDb;
    Character character = Character::VCA;
};

// The aliasing rows `python3 tools/dyn-lab/dynlab.py colour --character vca --oversampling N`, `--character fet` and
// `--character opto` print, copied as printed: each factor's worst alias anywhere and below the passband edge (1x's
// column is colourConditions()'s).
const std::vector<OversampledColour>& oversampledColourRows (Oversampling oversampling)
{
    static const std::vector<OversampledColour> x2 {
        { 44100.0, 1000.0, 10.0, -131.45, -170.07 }, { 44100.0, 1000.0, 20.0, -110.75, -149.44 },
        { 44100.0, 1000.0, 30.0, -118.96, -140.45 }, { 44100.0, 3000.0, 10.0, -131.06, -141.62 },
        { 44100.0, 3000.0, 20.0, -109.81, -127.01 }, { 44100.0, 3000.0, 30.0, -111.99, -111.99 },
        { 44100.0, 5000.0, 10.0, -104.75, -115.97 }, { 44100.0, 5000.0, 20.0, -93.73, -106.91 },
        { 44100.0, 5000.0, 30.0, -88.00, -88.19 }, { 44100.0, 10000.0, 10.0, -106.48, -108.34 },
        { 44100.0, 10000.0, 20.0, -81.83, -92.15 }, { 44100.0, 10000.0, 30.0, -70.45, -84.54 },
        { 48000.0, 1000.0, 10.0, -141.52, -174.51 }, { 48000.0, 1000.0, 20.0, -121.12, -156.35 },
        { 48000.0, 1000.0, 30.0, -109.65, -152.11 }, { 48000.0, 3000.0, 10.0, -122.41, -136.20 },
        { 48000.0, 3000.0, 20.0, -105.00, -126.55 }, { 48000.0, 3000.0, 30.0, -105.68, -108.85 },
        { 48000.0, 5000.0, 10.0, -76.14, -128.91 }, { 48000.0, 5000.0, 20.0, -65.09, -106.44 },
        { 48000.0, 5000.0, 30.0, -59.39, -94.36 }, { 48000.0, 10000.0, 10.0, -103.24, -103.24 },
        { 48000.0, 10000.0, 20.0, -85.03, -85.03 }, { 48000.0, 10000.0, 30.0, -88.24, -93.78 },
        { 44100.0, 1000.0, 10.0, -122.06, -160.93, Character::FET }, { 44100.0, 1000.0, 20.0, -101.09, -141.26, Character::FET },
        { 44100.0, 1000.0, 30.0, -108.79, -132.25, Character::FET }, { 44100.0, 3000.0, 10.0, -119.85, -122.70, Character::FET },
        { 44100.0, 3000.0, 20.0, -100.01, -112.88, Character::FET }, { 44100.0, 3000.0, 30.0, -104.61, -104.61, Character::FET },
        { 44100.0, 5000.0, 10.0, -98.13, -116.18, Character::FET }, { 44100.0, 5000.0, 20.0, -85.40, -98.56, Character::FET },
        { 44100.0, 5000.0, 30.0, -80.11, -80.29, Character::FET }, { 44100.0, 10000.0, 10.0, -80.61, -98.14, Character::FET },
        { 44100.0, 10000.0, 20.0, -76.91, -86.49, Character::FET }, { 44100.0, 10000.0, 30.0, -62.55, -76.63, Character::FET },
        { 48000.0, 1000.0, 10.0, -119.11, -163.42, Character::FET }, { 48000.0, 1000.0, 20.0, -108.14, -151.02, Character::FET },
        { 48000.0, 1000.0, 30.0, -102.36, -144.60, Character::FET }, { 48000.0, 3000.0, 10.0, -119.00, -129.81, Character::FET },
        { 48000.0, 3000.0, 20.0, -97.88, -120.45, Character::FET }, { 48000.0, 3000.0, 30.0, -97.48, -101.10, Character::FET },
        { 48000.0, 5000.0, 10.0, -69.57, -112.25, Character::FET }, { 48000.0, 5000.0, 20.0, -56.76, -100.29, Character::FET },
        { 48000.0, 5000.0, 30.0, -51.50, -86.36, Character::FET }, { 48000.0, 10000.0, 10.0, -93.07, -93.07, Character::FET },
        { 48000.0, 10000.0, 20.0, -77.17, -77.17, Character::FET }, { 48000.0, 10000.0, 30.0, -80.33, -86.22, Character::FET },
        // `--character opto --oversampling 2`.
        { 44100.0, 1000.0, 10.0, -134.79, -173.41, Character::Opto }, { 44100.0, 1000.0, 20.0, -114.19, -152.88, Character::Opto },
        { 44100.0, 1000.0, 30.0, -122.47, -143.96, Character::Opto }, { 44100.0, 3000.0, 10.0, -134.40, -144.96, Character::Opto },
        { 44100.0, 3000.0, 20.0, -113.24, -130.45, Character::Opto }, { 44100.0, 3000.0, 30.0, -115.50, -115.50, Character::Opto },
        { 44100.0, 5000.0, 10.0, -108.09, -119.31, Character::Opto }, { 44100.0, 5000.0, 20.0, -97.16, -110.34, Character::Opto },
        { 44100.0, 5000.0, 30.0, -91.50, -91.69, Character::Opto }, { 44100.0, 10000.0, 10.0, -109.81, -111.68, Character::Opto },
        { 44100.0, 10000.0, 20.0, -85.25, -95.57, Character::Opto }, { 44100.0, 10000.0, 30.0, -73.94, -88.02, Character::Opto },
        { 48000.0, 1000.0, 10.0, -144.86, -177.85, Character::Opto }, { 48000.0, 1000.0, 20.0, -124.56, -159.79, Character::Opto },
        { 48000.0, 1000.0, 30.0, -113.16, -155.62, Character::Opto }, { 48000.0, 3000.0, 10.0, -125.75, -139.54, Character::Opto },
        { 48000.0, 3000.0, 20.0, -108.43, -129.99, Character::Opto }, { 48000.0, 3000.0, 30.0, -109.19, -112.36, Character::Opto },
        { 48000.0, 5000.0, 10.0, -79.48, -132.25, Character::Opto }, { 48000.0, 5000.0, 20.0, -68.52, -109.88, Character::Opto },
        { 48000.0, 5000.0, 30.0, -62.90, -97.87, Character::Opto }, { 48000.0, 10000.0, 10.0, -106.58, -106.58, Character::Opto },
        { 48000.0, 10000.0, 20.0, -88.45, -88.45, Character::Opto }, { 48000.0, 10000.0, 30.0, -91.73, -97.26, Character::Opto },
    };
    static const std::vector<OversampledColour> x4 {
        { 44100.0, 1000.0, 10.0, -130.65, -204.18 }, { 44100.0, 1000.0, 20.0, -109.97, -183.87 },
        { 44100.0, 1000.0, 30.0, -118.05, -179.72 }, { 44100.0, 3000.0, 10.0, -167.69, -167.69 },
        { 44100.0, 3000.0, 20.0, -151.01, -151.01 }, { 44100.0, 3000.0, 30.0, -137.58, -137.58 },
        { 44100.0, 5000.0, 10.0, -103.71, -151.63 }, { 44100.0, 5000.0, 20.0, -92.54, -133.05 },
        { 44100.0, 5000.0, 30.0, -86.98, -122.66 }, { 44100.0, 10000.0, 10.0, -143.27, -143.27 },
        { 44100.0, 10000.0, 20.0, -110.07, -110.07 }, { 44100.0, 10000.0, 30.0, -100.84, -100.84 },
        { 48000.0, 1000.0, 10.0, -140.89, -209.52 }, { 48000.0, 1000.0, 20.0, -120.32, -191.42 },
        { 48000.0, 1000.0, 30.0, -108.93, -186.98 }, { 48000.0, 3000.0, 10.0, -121.55, -173.95 },
        { 48000.0, 3000.0, 20.0, -104.12, -152.77 }, { 48000.0, 3000.0, 30.0, -104.62, -148.18 },
        { 48000.0, 5000.0, 10.0, -75.27, -155.68 }, { 48000.0, 5000.0, 20.0, -64.09, -134.77 },
        { 48000.0, 5000.0, 30.0, -58.54, -123.64 }, { 48000.0, 10000.0, 10.0, -129.60, -132.41 },
        { 48000.0, 10000.0, 20.0, -107.20, -111.35 }, { 48000.0, 10000.0, 30.0, -95.10, -106.71 },
        { 44100.0, 1000.0, 10.0, -121.34, -191.58, Character::FET }, { 44100.0, 1000.0, 20.0, -100.32, -179.25, Character::FET },
        { 44100.0, 1000.0, 30.0, -107.91, -172.62, Character::FET }, { 44100.0, 3000.0, 10.0, -153.07, -153.07, Character::FET },
        { 44100.0, 3000.0, 20.0, -147.05, -147.05, Character::FET }, { 44100.0, 3000.0, 30.0, -129.28, -129.28, Character::FET },
        { 44100.0, 5000.0, 10.0, -97.32, -142.46, Character::FET }, { 44100.0, 5000.0, 20.0, -84.22, -124.19, Character::FET },
        { 44100.0, 5000.0, 30.0, -79.07, -114.52, Character::FET }, { 44100.0, 10000.0, 10.0, -117.56, -117.56, Character::FET },
        { 44100.0, 10000.0, 20.0, -102.99, -102.99, Character::FET }, { 44100.0, 10000.0, 30.0, -92.79, -92.79, Character::FET },
        { 48000.0, 1000.0, 10.0, -118.36, -196.30, Character::FET }, { 48000.0, 1000.0, 20.0, -107.36, -180.78, Character::FET },
        { 48000.0, 1000.0, 30.0, -101.65, -179.48, Character::FET }, { 48000.0, 3000.0, 10.0, -135.98, -160.56, Character::FET },
        { 48000.0, 3000.0, 20.0, -97.02, -143.48, Character::FET }, { 48000.0, 3000.0, 30.0, -96.41, -140.17, Character::FET },
        { 48000.0, 5000.0, 10.0, -68.89, -149.71, Character::FET }, { 48000.0, 5000.0, 20.0, -55.77, -124.45, Character::FET },
        { 48000.0, 5000.0, 30.0, -50.63, -115.35, Character::FET }, { 48000.0, 10000.0, 10.0, -113.00, -127.16, Character::FET },
        { 48000.0, 10000.0, 20.0, -101.06, -103.10, Character::FET }, { 48000.0, 10000.0, 30.0, -87.10, -98.46, Character::FET },
        // `--character opto --oversampling 4`.
        { 44100.0, 1000.0, 10.0, -133.99, -207.52, Character::Opto }, { 44100.0, 1000.0, 20.0, -113.41, -187.31, Character::Opto },
        { 44100.0, 1000.0, 30.0, -121.56, -183.23, Character::Opto }, { 44100.0, 3000.0, 10.0, -171.03, -171.03, Character::Opto },
        { 44100.0, 3000.0, 20.0, -154.45, -154.45, Character::Opto }, { 44100.0, 3000.0, 30.0, -141.09, -141.09, Character::Opto },
        { 44100.0, 5000.0, 10.0, -107.05, -154.97, Character::Opto }, { 44100.0, 5000.0, 20.0, -95.98, -136.49, Character::Opto },
        { 44100.0, 5000.0, 30.0, -90.49, -126.17, Character::Opto }, { 44100.0, 10000.0, 10.0, -146.61, -146.61, Character::Opto },
        { 44100.0, 10000.0, 20.0, -113.50, -113.50, Character::Opto }, { 44100.0, 10000.0, 30.0, -104.35, -104.35, Character::Opto },
        { 48000.0, 1000.0, 10.0, -144.23, -212.86, Character::Opto }, { 48000.0, 1000.0, 20.0, -123.75, -194.86, Character::Opto },
        { 48000.0, 1000.0, 30.0, -112.44, -190.49, Character::Opto }, { 48000.0, 3000.0, 10.0, -124.89, -177.29, Character::Opto },
        { 48000.0, 3000.0, 20.0, -107.56, -156.21, Character::Opto }, { 48000.0, 3000.0, 30.0, -108.13, -151.69, Character::Opto },
        { 48000.0, 5000.0, 10.0, -78.61, -159.02, Character::Opto }, { 48000.0, 5000.0, 20.0, -67.53, -138.21, Character::Opto },
        { 48000.0, 5000.0, 30.0, -62.05, -127.15, Character::Opto }, { 48000.0, 10000.0, 10.0, -132.94, -135.75, Character::Opto },
        { 48000.0, 10000.0, 20.0, -110.64, -114.78, Character::Opto }, { 48000.0, 10000.0, 30.0, -98.61, -110.22, Character::Opto },
    };
    static const std::vector<OversampledColour> x8 {
        { 44100.0, 1000.0, 10.0, -130.46, -222.50 }, { 44100.0, 1000.0, 20.0, -109.78, -207.89 },
        { 44100.0, 1000.0, 30.0, -117.82, -194.35 }, { 44100.0, 3000.0, 10.0, -183.20, -183.20 },
        { 44100.0, 3000.0, 20.0, -165.59, -165.59 }, { 44100.0, 3000.0, 30.0, -163.78, -163.78 },
        { 44100.0, 5000.0, 10.0, -103.46, -180.88 }, { 44100.0, 5000.0, 20.0, -92.26, -162.12 },
        { 44100.0, 5000.0, 30.0, -86.74, -150.93 }, { 44100.0, 10000.0, 10.0, -161.67, -163.39 },
        { 44100.0, 10000.0, 20.0, -143.08, -145.65 }, { 44100.0, 10000.0, 30.0, -132.69, -133.00 },
        { 48000.0, 1000.0, 10.0, -140.73, -222.21 }, { 48000.0, 1000.0, 20.0, -120.12, -208.27 },
        { 48000.0, 1000.0, 30.0, -108.76, -194.02 }, { 48000.0, 3000.0, 10.0, -121.34, -197.29 },
        { 48000.0, 3000.0, 20.0, -103.91, -179.11 }, { 48000.0, 3000.0, 30.0, -104.36, -177.39 },
        { 48000.0, 5000.0, 10.0, -75.06, -187.71 }, { 48000.0, 5000.0, 20.0, -63.85, -166.45 },
        { 48000.0, 5000.0, 30.0, -58.34, -156.46 }, { 48000.0, 10000.0, 10.0, -149.76, -149.76 },
        { 48000.0, 10000.0, 20.0, -141.54, -141.54 }, { 48000.0, 10000.0, 30.0, -140.19, -140.19 },
        { 44100.0, 1000.0, 10.0, -121.17, -216.38, Character::FET }, { 44100.0, 1000.0, 20.0, -100.13, -201.66, Character::FET },
        { 44100.0, 1000.0, 30.0, -107.70, -186.49, Character::FET }, { 44100.0, 3000.0, 10.0, -185.33, -185.33, Character::FET },
        { 44100.0, 3000.0, 20.0, -158.72, -158.72, Character::FET }, { 44100.0, 3000.0, 30.0, -155.63, -155.63, Character::FET },
        { 44100.0, 5000.0, 10.0, -97.12, -167.57, Character::FET }, { 44100.0, 5000.0, 20.0, -83.94, -157.86, Character::FET },
        { 44100.0, 5000.0, 30.0, -78.82, -143.04, Character::FET }, { 44100.0, 10000.0, 10.0, -152.48, -152.64, Character::FET },
        { 44100.0, 10000.0, 20.0, -133.59, -133.59, Character::FET }, { 44100.0, 10000.0, 30.0, -124.55, -124.56, Character::FET },
        { 48000.0, 1000.0, 10.0, -118.18, -217.73, Character::FET }, { 48000.0, 1000.0, 20.0, -107.17, -202.56, Character::FET },
        { 48000.0, 1000.0, 30.0, -101.48, -186.18, Character::FET }, { 48000.0, 3000.0, 10.0, -136.48, -185.29, Character::FET },
        { 48000.0, 3000.0, 20.0, -96.81, -170.70, Character::FET }, { 48000.0, 3000.0, 30.0, -96.15, -170.20, Character::FET },
        { 48000.0, 5000.0, 10.0, -68.72, -172.12, Character::FET }, { 48000.0, 5000.0, 20.0, -55.53, -155.14, Character::FET },
        { 48000.0, 5000.0, 30.0, -50.41, -147.98, Character::FET }, { 48000.0, 10000.0, 10.0, -139.73, -139.73, Character::FET },
        { 48000.0, 10000.0, 20.0, -134.00, -134.00, Character::FET }, { 48000.0, 10000.0, 30.0, -131.43, -131.43, Character::FET },
        // `--character opto --oversampling 8`.
        { 44100.0, 1000.0, 10.0, -133.80, -225.84, Character::Opto }, { 44100.0, 1000.0, 20.0, -113.22, -211.33, Character::Opto },
        { 44100.0, 1000.0, 30.0, -121.33, -197.86, Character::Opto }, { 44100.0, 3000.0, 10.0, -186.54, -186.54, Character::Opto },
        { 44100.0, 3000.0, 20.0, -169.02, -169.02, Character::Opto }, { 44100.0, 3000.0, 30.0, -167.29, -167.29, Character::Opto },
        { 44100.0, 5000.0, 10.0, -106.80, -184.22, Character::Opto }, { 44100.0, 5000.0, 20.0, -95.69, -165.56, Character::Opto },
        { 44100.0, 5000.0, 30.0, -90.25, -154.44, Character::Opto }, { 44100.0, 10000.0, 10.0, -165.01, -166.73, Character::Opto },
        { 44100.0, 10000.0, 20.0, -146.52, -149.09, Character::Opto }, { 44100.0, 10000.0, 30.0, -136.20, -136.51, Character::Opto },
        { 48000.0, 1000.0, 10.0, -144.08, -225.55, Character::Opto }, { 48000.0, 1000.0, 20.0, -123.56, -211.71, Character::Opto },
        { 48000.0, 1000.0, 30.0, -112.27, -197.53, Character::Opto }, { 48000.0, 3000.0, 10.0, -124.68, -200.63, Character::Opto },
        { 48000.0, 3000.0, 20.0, -107.35, -182.54, Character::Opto }, { 48000.0, 3000.0, 30.0, -107.87, -180.90, Character::Opto },
        { 48000.0, 5000.0, 10.0, -78.40, -191.05, Character::Opto }, { 48000.0, 5000.0, 20.0, -67.29, -169.89, Character::Opto },
        { 48000.0, 5000.0, 30.0, -61.85, -159.97, Character::Opto }, { 48000.0, 10000.0, 10.0, -153.10, -153.10, Character::Opto },
        { 48000.0, 10000.0, 20.0, -144.97, -144.97, Character::Opto }, { 48000.0, 10000.0, 30.0, -143.70, -143.70, Character::Opto },
    };
    return oversampling == Oversampling::x2 ? x2 : oversampling == Oversampling::x4 ? x4 : x8;
}

// The same condition's row at the base rate.
const ColourCondition& atBase (const OversampledColour& row)
{
    for (const auto& condition : colourConditions())
        if (condition.sampleRate == row.sampleRate && condition.frequency == row.frequency && condition.reductionDb == row.reductionDb
            && condition.character == row.character)
            return condition;
    FAIL ("no base-rate row for the condition");
    return colourConditions().front();
}

struct OversampledColourCondition
{
    Oversampling oversampling;
    OversampledColour row;
};

std::vector<OversampledColourCondition> oversampledColourConditions()
{
    std::vector<OversampledColourCondition> all;
    for (const auto oversampling : { Oversampling::x2, Oversampling::x4, Oversampling::x8 })
        for (const auto& row : oversampledColourRows (oversampling))
            all.push_back ({ oversampling, row });
    return all;
}

} // namespace

TEST_CASE ("Oversampled, VCA's, FET's and Opto's worst alias at Colour 100% at each of dyn-lab's Colour conditions is within its factor's tolerance and no worse than at 1x", "[sweep]")
{
    const auto conditions = oversampledColourConditions();
    const auto& [oversampling, row] = GENERATE_REF (from_range (conditions));
    CAPTURE (factorOf (oversampling), row.sampleRate, row.frequency, row.reductionDb, static_cast<int> (row.character));
    const int bin = test::toneBin (row.frequency, row.sampleRate);
    const auto spectrum = test::amplitudes (test::steadilyCompressedTone (
        { .character = row.character, .processingMode = ProcessingMode::HighQuality, .oversampling = oversampling }, row.sampleRate, bin,
        test::levelForReductionDb (row.reductionDb), 8));
    const double worst = test::worstAliasDb (spectrum, bin);
    const auto top = static_cast<std::size_t> (passbandEdge * test::spectrumLength / 2);
    const double passband = test::worstAliasDb ({ spectrum.begin(), spectrum.begin() + static_cast<std::ptrdiff_t> (top + 1) }, bin);
    CHECK (passband <= passbandCeilingDb (oversampling));
    CHECK (worst <= atBase (row).labDb);
    if (row.labDb > colourComparedOverDb)
        CHECK (std::abs (worst - row.labDb) <= labWithinDb);
    if (row.passbandDb > colourComparedOverDb)
        CHECK (std::abs (passband - row.passbandDb) <= labWithinDb);
}
