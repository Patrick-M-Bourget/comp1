#pragma once

// A gain applied to every channel, in dB: Auto Gain, and Output Gain. It glides to each new target with plugkit's
// Smoother, so a change, Host Automation included, is click-free.

#include "Glide.h"
#include "comp1/Engine.h"

#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

namespace comp1
{

class GlidingGain
{
public:
    void prepare (double sampleRate)
    {
        gain.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        gain.reset (1.0);
    }

    // With snap, the new gain takes effect at once instead of gliding.
    void setTargetDb (double db, bool snap)
    {
        glideTo (gain, plugkit::decibelsToGain (db), snap);
    }

    void process (AudioBlock block)
    {
        // Settled on exactly 1 at 0 dB, so the output is the input, bit for bit, with no work.
        if (gain.isSettled() && gain.value() == 1.0)
            return;
        // The gain glides a sample at a time; once it has settled, next() would return its goal exactly, so the rest
        // is scaled by that as a constant, sample for sample the same.
        int i = 0;
        for (; i < block.numSamples && ! gain.isSettled(); ++i)
        {
            const double g = gain.next();
            for (int ch = 0; ch < block.numChannels; ++ch)
                block.channels[ch][i] = static_cast<float> (g * block.channels[ch][i]);
        }
        const double g = gain.value();
        for (int ch = 0; ch < block.numChannels; ++ch)
            for (int n = i; n < block.numSamples; ++n)
                block.channels[ch][n] = static_cast<float> (g * block.channels[ch][n]);
    }

private:
    plugkit::Smoother gain;
};

} // namespace comp1
