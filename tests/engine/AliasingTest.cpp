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

// The rows `python3 tools/dyn-lab/dynlab.py aliasing` prints, copied as printed. Oversampling adds tables of its own
// below, and Colour (#19) will.
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

TEST_CASE ("At the base rate, the worst alias at each of dyn-lab's reference conditions is within its tolerance")
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

TEST_CASE ("Oversampled, the worst alias at each of dyn-lab's reference conditions is within its factor's tolerance and no worse than at 1x")
{
    const auto conditions = oversampledConditions();
    const auto& [oversampling, row] = GENERATE_REF (from_range (conditions));
    const auto position = GENERATE (SaturationPosition::Before, SaturationPosition::After);
    CAPTURE (factorOf (oversampling), row.sampleRate, row.frequency, row.driveDb, static_cast<int> (position));
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

TEST_CASE ("At the base rate, the in-band harmonics are within dyn-lab's tolerance of its ideal reference")
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
