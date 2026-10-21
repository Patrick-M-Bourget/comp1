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

// VCA's Colour mapping: dyn-lab's VCA_COLOUR_WEIGHT, VCA_COLOUR_WEIGHT_DB and VCA_COLOUR_DRIVE_DB.
inline constexpr ColourMapping colour { .weight = 0.11, .weightDb = 10.0, .driveDb = 5.0 };

} // namespace comp1::vca
