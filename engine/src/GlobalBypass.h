#pragma once

// Global Bypass: crossfades the output to the reference, the input after the non-finite scrub, delayed by the latency
// of the Processing Mode playing (comp1/Latency.h), so the processed output and the reference have the same delay and
// the crossfade never comb-filters. The reference's delay is fed in both Processing Modes, so a switch to High
// Quality starts from real recent audio.

#include "Crossfade.h"
#include "DelayLine.h"
#include "Glide.h"
#include "comp1/Engine.h"
#include "comp1/Latency.h"
#include "comp1/Settings.h"

#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace comp1
{

class GlobalBypass
{
public:
    // Allocates for up to numChannels channels of up to a run a piece.
    void prepare (double sampleRate, int numChannels)
    {
        share.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        share.reset (0.0);
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        storage.assign (channels, std::vector<float> (static_cast<std::size_t> (plugkit::runSamples)));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        delay.prepare (numChannels, latencySamples (ProcessingMode::HighQuality, sampleRate));
    }

    // With snap, it is on or off at once instead of crossfading.
    void setOn (bool on, bool snap) { glideTo (share, on ? 1.0 : 0.0, snap); }

    // Takes the piece of input, before anything processes it, as the reference for the same samples' output, delayed
    // on path.
    void takeInput (ConstAudioBlock input, ProcessingMode path)
    {
        const AudioBlock taken { pointers.data(), input.numChannels, input.numSamples };
        for (int ch = 0; ch < input.numChannels; ++ch)
            std::copy (input.channels[ch], input.channels[ch] + input.numSamples, taken.channels[ch]);
        delay.process ({ taken.channels, taken.numChannels, taken.numSamples }, taken, path == ProcessingMode::HighQuality ? delay.getLength() : 0);
    }

    // Crossfades the processed piece of the same samples toward the reference.
    void process (AudioBlock processed)
    {
        crossfade (share, processed, { pointers.data(), processed.numChannels, processed.numSamples });
    }

private:
    plugkit::Smoother share;                 // 0 plays the processed output, 1 the reference
    std::vector<std::vector<float>> storage; // per channel, the reference for the piece
    std::vector<float*> pointers;            // per channel, into storage
    DelayLine delay;                         // High Quality's, per channel
};

} // namespace comp1
