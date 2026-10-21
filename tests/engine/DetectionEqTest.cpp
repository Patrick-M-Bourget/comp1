#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <plugkit/ShapeDesign.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

using namespace comp1;

namespace
{

constexpr double toneAmplitude = 0.5;

// The detection signal's level in dB, against the input's, for a steady sine at frequency through an Engine with
// settings and Detection Audition held from the start: least squares fits a sine and a cosine at frequency to the
// last measureSeconds, once settleSeconds have let every filter settle. The fit is exact for a pure tone however many
// periods the window holds.
double auditionedGainDb (Settings settings, double sampleRate, double frequency)
{
    constexpr double settleSeconds = 1.0, measureSeconds = 0.25;
    settings.detectionAudition = true;
    const int settle = static_cast<int> (settleSeconds * sampleRate), measure = static_cast<int> (measureSeconds * sampleRate);
    test::Channels audio (1, std::vector<float> (static_cast<std::size_t> (settle + measure)));
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    for (std::size_t n = 0; n < audio[0].size(); ++n)
        audio[0][n] = static_cast<float> (toneAmplitude * std::sin (w * static_cast<double> (n)));
    Engine engine;
    engine.prepare (sampleRate, 512, 1);
    engine.setSettings (settings);
    test::processInBlocks (engine, audio, { 512 });

    double ss = 0.0, sc = 0.0, cc = 0.0, ys = 0.0, yc = 0.0;
    for (int n = settle; n < settle + measure; ++n)
    {
        const double s = std::sin (w * n), c = std::cos (w * n), y = audio[0][static_cast<std::size_t> (n)];
        ss += s * s;
        sc += s * c;
        cc += c * c;
        ys += y * s;
        yc += y * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    return 20.0 * std::log10 (std::hypot (a, b) / toneAmplitude);
}

// The product of plugkit's design responses over the bands that are on, in dB.
double designedGainDb (const DetectionEq& eq, double sampleRate, double frequency)
{
    std::complex<double> h = 1.0;
    for (const auto* band : { &eq.highPass, &eq.lowPass, &eq.band })
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

DetectionEq freeBand (DetectionBandShape shape, double frequency, double gain, double q)
{
    DetectionEq eq;
    eq.band.on = true;
    eq.band.filter.shape = shapeOf (shape);
    eq.band.filter.frequency = frequency;
    eq.band.filter.gain = gain;
    eq.band.filter.q = q;
    return eq;
}

DetectionEq allThree()
{
    auto eq = freeBand (DetectionBandShape::Bell, 2000.0, -12.0, 4.0);
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
        { "Bell 1 kHz, +9 dB, Q 2", freeBand (DetectionBandShape::Bell, 1000.0, 9.0, 2.0) },
        { "Low Shelf 300 Hz, -6 dB, Q 0.7", freeBand (DetectionBandShape::LowShelf, 300.0, -6.0, 0.7) },
        { "High Shelf 3 kHz, +6 dB, Q 1", freeBand (DetectionBandShape::HighShelf, 3000.0, 6.0, 1.0) },
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
