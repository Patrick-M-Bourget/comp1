#pragma once

// Colour: a Character's own distortion, which grows with its Gain Reduction (dyn-lab's USAGE.md, "Colour"). The
// saturation module's shape and anti-aliased residual (Saturation.h), unchanged, driven by the Character's mapping of
// the Gain Reduction applied, sample by sample. Like the module, it assumes nothing about the sample rate, so it runs
// as well oversampled (#10).

#include "Saturation.h"

#include <plugkit/Units.h>

namespace comp1
{

// A Character's Colour at a Gain Reduction: the residual's weight and the drive in dB.
struct ColourAt
{
    double weight = 0.0;
    double driveDb = 0.0;
};

// Colour on one part the compressor compresses (a channel, or the Mid or the Side). Each compressed sample y gains
// w r(g y) / g, with g the drive's gain and r the residual anti-aliased (ADR 0005), so the linear part, y, passes
// sample-aligned. With a weight of exactly 0 it adds exactly nothing, while the ADAA state follows the driven signal, so
// Colour rising has no transient. Never allocates.
class ColourStage
{
public:
    // Starts afresh: the next sample's ADAA takes it as its last.
    void reset() { fresh = true; }

    // What Colour adds to the compressed sample at colour.
    double change (double compressed, const ColourAt& colour)
    {
        const double g = plugkit::decibelsToGain (colour.driveDb);
        const double u = g * compressed;
        if (fresh || colour.weight == 0.0)
        {
            residual.follow (u);
            fresh = false;
            if (colour.weight == 0.0)
                return 0.0;
        }
        return colour.weight * residual.process (u) / g;
    }

private:
    saturation::AntiAliased residual;
    bool fresh = true;
};

} // namespace comp1
