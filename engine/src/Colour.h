#pragma once

// Colour: a Character's own distortion, which grows with its Gain Reduction (dyn-lab's USAGE.md, "Colour"). The
// saturation module's shape and anti-aliased residual (Saturation.h), unchanged, driven by the Character's mapping of
// the Gain Reduction applied, sample by sample. Like the module, it assumes nothing about the sample rate, so it runs
// as well oversampled (#10).

#include "Saturation.h"

#include "comp1/Settings.h"

#include <plugkit/Units.h>

#include <cmath>

namespace comp1
{

// A Character's Colour at a Gain Reduction: the residual's weight and the drive in dB.
struct ColourAt
{
    double weight = 0.0;
    double driveDb = 0.0;
};

// A Character's Colour mapping (dyn-lab's COLOUR_MAPPINGS): the most its weight reaches, and the Gain Reductions in dB
// over which its weight and its drive rise.
struct ColourMapping
{
    double weight = 0.0;
    double weightDb = 1.0;
    double driveDb = 1.0;
};

// The Colour mapping at a Gain Reduction in dB, before the Colour control scales its weight: the weight mapping.weight ×
// (1 - e^(-GR / mapping.weightDb)), and the drive Drive's top × (1 - e^(-GR / mapping.driveDb)), which nears it and
// never passes it. Each is exactly 0 with no Gain Reduction, and rises smoothly with it.
inline ColourAt colourAt (const ColourMapping& mapping, double gainReductionDb)
{
    if (! (gainReductionDb > 0.0))
        return {};
    return { .weight = mapping.weight * -std::expm1 (-gainReductionDb / mapping.weightDb),
             .driveDb = parameterRanges::drive.high * -std::expm1 (-gainReductionDb / mapping.driveDb) };
}

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
