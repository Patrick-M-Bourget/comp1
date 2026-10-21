#pragma once

// The VCA Character's own pieces, as dyn-lab models them (tools/dyn-lab/USAGE.md, "VCA" and "Colour"): its RMS
// Detection Level, which feeds Clean's gain computer and detector unchanged, and its Colour mapping. Kept apart from
// the rest of the Engine, as Clean's are.

#include "Colour.h"

#include "comp1/Settings.h"

#include <plugkit/Units.h>

#include <cmath>

namespace comp1::vca
{

// The RMS Detection Level of one detection channel: a one-pole mean-square average of the detection signal, read
// sine-calibrated, 10·log10(2 · mean square) dB, so a sine reads its peak level, as on Clean, and a constant-magnitude
// signal 3.01 dB over its peak level. -infinity dB (silence) when the mean square is 0.
class RmsLevel
{
public:
    // dyn-lab's VCA_RMS_MS: the mean square covers 1 - 1/e of a step in this time.
    static constexpr double timeMs = 5.0;

    void prepare (double sampleRate)
    {
        pole = std::exp (-1000.0 / (timeMs * sampleRate));
        reset();
    }

    void reset() { meanSquare = 0.0; }

    // Takes one detection sample and returns the Detection Level in dB.
    double process (float sample)
    {
        const double x = sample;
        meanSquare = pole * meanSquare + (1.0 - pole) * x * x;
        return plugkit::powerToDecibels (2.0 * meanSquare);
    }

private:
    double pole = 0.0;
    double meanSquare = 0.0;
};

// dyn-lab's VCA_COLOUR_WEIGHT, VCA_COLOUR_WEIGHT_DB and VCA_COLOUR_DRIVE_DB.
inline constexpr double colourWeight = 0.11;
inline constexpr double colourWeightDb = 10.0;
inline constexpr double colourDriveDb = 5.0;

// VCA's Colour at a Gain Reduction in dB, before the Colour control scales its weight: the weight colourWeight ×
// (1 - e^(-GR / colourWeightDb)), and the drive Drive's top × (1 - e^(-GR / colourDriveDb)), which nears it and never
// passes it. Each is exactly 0 with no Gain Reduction, and rises smoothly with it.
inline ColourAt colourAt (double gainReductionDb)
{
    if (! (gainReductionDb > 0.0))
        return {};
    return { .weight = colourWeight * -std::expm1 (-gainReductionDb / colourWeightDb),
             .driveDb = parameterRanges::drive.high * -std::expm1 (-gainReductionDb / colourDriveDb) };
}

} // namespace comp1::vca
