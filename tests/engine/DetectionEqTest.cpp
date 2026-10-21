#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/ShapeDesign.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

// A steady sine of amplitude at frequency, seconds long.
std::vector<float> tone (double amplitude, double frequency, double sampleRate, double seconds)
{
    std::vector<float> samples (static_cast<std::size_t> (seconds * sampleRate));
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    for (std::size_t n = 0; n < samples.size(); ++n)
        samples[n] = static_cast<float> (amplitude * std::sin (w * static_cast<double> (n)));
    return samples;
}

// The amplitude of the sine at frequency in the samples from `from` on: least squares fits a sine and a cosine at
// frequency to them, which is exact for a pure tone however many periods they hold.
double fittedAmplitude (const std::vector<float>& samples, double frequency, double sampleRate, std::size_t from)
{
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    double ss = 0.0, sc = 0.0, cc = 0.0, ys = 0.0, yc = 0.0;
    for (std::size_t n = from; n < samples.size(); ++n)
    {
        const double s = std::sin (w * static_cast<double> (n)), c = std::cos (w * static_cast<double> (n));
        const double y = samples[n];
        ss += s * s;
        sc += s * c;
        cc += c * c;
        ys += y * s;
        yc += y * c;
    }
    const double det = ss * cc - sc * sc;
    return std::hypot ((ys * cc - yc * sc) / det, (yc * ss - ys * sc) / det);
}

constexpr double toneAmplitude = 0.5;

// The detection signal's level in dB, against the input's, for a steady sine at frequency through an Engine with
// settings and Detection Audition held from the start, measured over the last measureSeconds, once settleSeconds have
// let every filter settle.
double auditionedGainDb (Settings settings, double sampleRate, double frequency)
{
    constexpr double settleSeconds = 1.0, measureSeconds = 0.25;
    settings.detectionAudition = true;
    test::Channels audio { tone (toneAmplitude, frequency, sampleRate, settleSeconds + measureSeconds) };
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings (settings);
    test::processInBlocks (engine, audio, { 512 });
    const auto from = static_cast<std::size_t> (settleSeconds * sampleRate);
    return 20.0 * std::log10 (fittedAmplitude (audio[0], frequency, sampleRate, from) / toneAmplitude);
}

// The product of plugkit's design responses over the bands that are on, in dB.
double designedGainDb (const DetectionEq& eq, double sampleRate, double frequency)
{
    std::complex<double> h = 1.0;
    for (const auto* band : { &eq.highPass, &eq.lowPass, &eq.freeBand })
        if (band->on)
            h *= plugkit::responseAt (plugkit::designBand (band->filter, sampleRate), frequency, sampleRate);
    return 20.0 * std::log10 (std::abs (h));
}

// Frequencies from 20 Hz to 0.45 x Nyquist, a sixth of an octave apart, the top one included.
std::vector<double> measuredFrequencies (double sampleRate)
{
    const double top = 0.45 * sampleRate / 2.0;
    std::vector<double> frequencies;
    for (double f = 20.0; f < top; f *= std::pow (2.0, 1.0 / 6.0))
        frequencies.push_back (f);
    frequencies.push_back (top);
    return frequencies;
}

// Within this of plugkit's design response, where the design passes at least floorDb: deeper than that a float
// signal's rounding is louder than what the filter lets through, so there it must only stay under floorDb.
constexpr double toleranceDb = 0.05, floorDb = -80.0;

struct Case
{
    std::string name;
    DetectionEq eq;
};

DetectionEq highPass (double frequency, double slope)
{
    DetectionEq eq;
    eq.highPass.on = true;
    eq.highPass.filter.frequency = frequency;
    eq.highPass.filter.slope = slope;
    return eq;
}

DetectionEq lowPass (double frequency, double slope)
{
    DetectionEq eq;
    eq.lowPass.on = true;
    eq.lowPass.filter.frequency = frequency;
    eq.lowPass.filter.slope = slope;
    return eq;
}

DetectionEq freeBand (FreeBandShape shape, double frequency, double gain, double q)
{
    DetectionEq eq;
    eq.freeBand.on = true;
    eq.freeBand.filter.shape = shapeOf (shape);
    eq.freeBand.filter.frequency = frequency;
    eq.freeBand.filter.gain = gain;
    eq.freeBand.filter.q = q;
    return eq;
}

DetectionEq allThree()
{
    auto eq = freeBand (FreeBandShape::Bell, 2000.0, -12.0, 4.0);
    eq.highPass = highPass (80.0, 24.0).highPass;
    eq.lowPass = lowPass (8000.0, 18.0).lowPass;
    return eq;
}

const std::vector<Case>& cases()
{
    static const std::vector<Case> all {
        { "high-pass 100 Hz, 6 dB/oct", highPass (100.0, 6.0) },
        { "high-pass 100 Hz, 48 dB/oct", highPass (100.0, 48.0) },
        { "high-pass 200 Hz, 96 dB/oct", highPass (200.0, 96.0) },
        { "low-pass 5 kHz, 12 dB/oct", lowPass (5000.0, 12.0) },
        { "low-pass 2 kHz, 36 dB/oct", lowPass (2000.0, 36.0) },
        { "Bell 1 kHz, +9 dB, Q 2", freeBand (FreeBandShape::Bell, 1000.0, 9.0, 2.0) },
        { "Low Shelf 300 Hz, -6 dB, Q 0.7", freeBand (FreeBandShape::LowShelf, 300.0, -6.0, 0.7) },
        { "High Shelf 3 kHz, +6 dB, Q 1", freeBand (FreeBandShape::HighShelf, 3000.0, 6.0, 1.0) },
        { "all three", allThree() },
    };
    return all;
}

} // namespace

TEST_CASE ("Through Detection Audition, the detection signal's response is plugkit's design response of the bands that are on")
{
    const auto& c = GENERATE (from_range (cases()));
    const double sampleRate = GENERATE (44100.0, 96000.0, 192000.0);
    CAPTURE (c.name, sampleRate);
    Settings settings;
    settings.detectionEq = c.eq;
    for (const double f : measuredFrequencies (sampleRate))
    {
        const double designed = designedGainDb (c.eq, sampleRate, f);
        const double measured = auditionedGainDb (settings, sampleRate, f);
        CAPTURE (f, designed, measured);
        if (designed >= floorDb)
            CHECK (std::abs (measured - designed) < toleranceDb);
        else
            CHECK (measured < floorDb + toleranceDb);
    }
}

namespace
{

// Every band on, each away from its defaults: a Detection EQ that changes the detection signal throughout.
DetectionEq everyBandOn()
{
    auto eq = freeBand (FreeBandShape::HighShelf, 1500.0, 12.0, 3.0);
    eq.highPass = highPass (400.0, 30.0).highPass;
    eq.lowPass = lowPass (3000.0, 6.0).lowPass;
    return eq;
}

// The output of a stereo Engine at 48 kHz given settings from the start, for noise on the main input and, when
// withSidechain, other noise on a stereo Sidechain.
test::Channels play (const Settings& settings, bool withSidechain)
{
    auto audio = test::noise (2, 24000);
    const auto sidechainSamples = test::noise (2, 24000, 3);
    Engine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setSettings (settings);
    test::processInBlocks (engine, audio, { 512 }, withSidechain ? &sidechainSamples : nullptr);
    return audio;
}

} // namespace

TEST_CASE ("With every band off, the output and Detection Audition are those of no Detection EQ, whatever the bands hold")
{
    const auto source = GENERATE (DetectionSource::Internal, DetectionSource::External);
    const bool audition = GENERATE (false, true);
    CAPTURE (static_cast<int> (source), audition);
    const Settings plain { .thresholdDb = -30.0, .ratio = 8.0, .detectionSource = source, .detectionAudition = audition };
    auto off = plain;
    off.detectionEq = everyBandOn();
    off.detectionEq.highPass.on = off.detectionEq.lowPass.on = off.detectionEq.freeBand.on = false;
    CHECK (play (off, true) == play (plain, true));
    // The bands on change it: the test can tell.
    auto on = off;
    on.detectionEq = everyBandOn();
    CHECK (play (on, true) != play (plain, true));
}

TEST_CASE ("With no Gain Reduction, the output is the input, bit for bit, at every Detection EQ setting")
{
    const auto source = GENERATE (DetectionSource::Internal, DetectionSource::External);
    const bool ratioOne = GENERATE (true, false);
    const int bandsOn = GENERATE (range (0, 8)); // a bit per band
    CAPTURE (static_cast<int> (source), ratioOne, bandsOn);
    Settings settings { .thresholdDb = -60.0, .detectionSource = source };
    if (ratioOne)
        settings.ratio = 1.0;
    else
        settings.rangeDb = 0.0;
    settings.detectionEq = everyBandOn();
    settings.detectionEq.highPass.on = (bandsOn & 1) != 0;
    settings.detectionEq.lowPass.on = (bandsOn & 2) != 0;
    settings.detectionEq.freeBand.on = (bandsOn & 4) != 0;
    CHECK (play (settings, true) == test::noise (2, 24000));
}

TEST_CASE ("A high-pass over a loud low tone stops the compressor reacting to it")
{
    // A full-scale 50 Hz sine: the Detection Level reads 0 dB. The detector holds its peak: Attack at its fastest,
    // Hold longer than the tone's period, Release at its slowest. dyn-lab's Transfer Curve at Threshold -18, Ratio 4:1
    // and Knee 0 takes off 3/4 of the level over Threshold, nothing below it (python3 tools/dyn-lab/dynlab.py transfer
    // --threshold -18 --ratio 4 --knee 0): 13.5 dB at 0 dB.
    constexpr double sampleRate = 48000.0, frequency = 50.0, seconds = 1.5;
    const auto transferReductionDb = [] (double levelDb) { return std::max (0.0, 0.75 * (levelDb + 18.0)); };
    Settings settings { .thresholdDb = -18.0, .ratio = 4.0, .kneeDb = 0.0, .attackMs = 0.005, .releaseMs = 5000.0, .holdMs = 500.0 };
    // Off, then at 200 Hz: 12 dB/oct takes the tone under Threshold, 6 dB/oct only part of the way.
    const double slope = GENERATE (0.0, 12.0, 6.0);
    CAPTURE (slope);
    if (slope > 0.0)
        settings.detectionEq = highPass (200.0, slope);
    const double filteredDb = designedGainDb (settings.detectionEq, sampleRate, frequency);
    const double expected = transferReductionDb (filteredDb);
    CAPTURE (filteredDb, expected);
    if (slope == 0.0)
        REQUIRE (expected == 13.5);
    if (slope == 12.0)
        REQUIRE (expected == 0.0);
    if (slope == 6.0)
        REQUIRE ((expected > 1.0 && expected < 13.0));

    const auto input = tone (1.0, frequency, sampleRate, seconds);
    test::Channels audio { input };
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings (settings);
    test::processInBlocks (engine, audio, { 512 });
    const auto from = static_cast<std::size_t> (1.0 * sampleRate);
    const double reductionDb = -20.0 * std::log10 (fittedAmplitude (audio[0], frequency, sampleRate, from));
    CHECK (std::abs (reductionDb - expected) < 0.05);
}
