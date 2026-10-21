#pragma once

// The one crossfade the Engine's switches share: Global Bypass, and Detection Audition. A piece of audio moves toward
// a reference by a share that glides with plugkit's Smoother, a sample at a time, so it moves the same however the
// audio is cut.

#include "comp1/Engine.h"

#include <plugkit/Smoother.h>

#include <algorithm>

namespace comp1
{

// Crossfades piece, in place, toward reference, of the same channels and samples, by share: each sample is
// (1 - share) of piece's and share of reference's. share glides between 0 and 1. Settled at 0, piece is left as it
// is, bit for bit, with no work; settled at 1, it becomes reference exactly, with no arithmetic on the samples.
inline void crossfade (plugkit::Smoother& share, AudioBlock piece, ConstAudioBlock reference)
{
    if (share.isSettled() && share.value() == 0.0)
        return;
    int i = 0;
    for (; i < piece.numSamples && ! share.isSettled(); ++i)
    {
        const double s = share.next();
        for (int ch = 0; ch < piece.numChannels; ++ch)
        {
            float& sample = piece.channels[ch][i];
            sample = static_cast<float> ((1.0 - s) * sample + s * reference.channels[ch][i]);
        }
    }
    // Settled part-way: at 0 the rest is left as it is, at 1 it is the reference's.
    if (share.value() == 0.0)
        return;
    for (int ch = 0; ch < piece.numChannels; ++ch)
        std::copy (reference.channels[ch] + i, reference.channels[ch] + piece.numSamples, piece.channels[ch] + i);
}

} // namespace comp1
