#pragma once

// A delay line of a length fixed at prepare(), on every channel at once: High Quality's Lookahead delays and the
// reserves' padding. It allocates only in prepare(), so process() and write() are safe on the audio thread.

#include "comp1/Engine.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace comp1
{

class DelayLine
{
public:
    // Allocates for numChannels channels delaying by up to length samples, and fills them with silence.
    void prepare (int numChannels, int newLength)
    {
        length = std::max (newLength, 0);
        // One slot more than the length, so a sample written can be read back length samples later.
        buffers.assign (static_cast<std::size_t> (std::max (numChannels, 0)), std::vector<float> (static_cast<std::size_t> (length + 1)));
        position = 0;
    }

    int getLength() const { return length; }

    // Delays block in place by the full length.
    void process (AudioBlock block) { process ({ block.channels, block.numChannels, block.numSamples }, block, length); }

    // Feeds in's samples in, and writes to out the samples delay earlier, delay from 0 to the length; 0 passes in
    // through as it is. in and out may be the same block, and hold the same channels and samples, at most the
    // prepared channels.
    void process (ConstAudioBlock in, AudioBlock out, int delay)
    {
        assert (in.numChannels <= static_cast<int> (buffers.size()) && in.numChannels == out.numChannels && in.numSamples == out.numSamples);
        assert (delay >= 0 && delay <= length);
        const int size = length + 1;
        for (int ch = 0; ch < in.numChannels; ++ch)
        {
            float* buffer = buffers[static_cast<std::size_t> (ch)].data();
            int write = position;
            int read = write - delay < 0 ? write - delay + size : write - delay;
            for (int i = 0; i < in.numSamples; ++i)
            {
                buffer[write] = in.channels[ch][i];
                out.channels[ch][i] = buffer[read];
                write = write + 1 == size ? 0 : write + 1;
                read = read + 1 == size ? 0 : read + 1;
            }
        }
        advance (in.numSamples);
    }

    // Feeds block's samples in without reading any out, so the line holds the latest audio when it is read again.
    void write (ConstAudioBlock block)
    {
        assert (block.numChannels <= static_cast<int> (buffers.size()));
        const int size = length + 1;
        for (int ch = 0; ch < block.numChannels; ++ch)
        {
            float* buffer = buffers[static_cast<std::size_t> (ch)].data();
            int write = position;
            for (int i = 0; i < block.numSamples; ++i)
            {
                buffer[write] = block.channels[ch][i];
                write = write + 1 == size ? 0 : write + 1;
            }
        }
        advance (block.numSamples);
    }

private:
    void advance (int samples) { position = (position + samples) % (length + 1); }

    int length = 0;
    std::vector<std::vector<float>> buffers; // per channel, a ring of length + 1 samples
    int position = 0;                        // where the next sample is written, the same on every channel
};

} // namespace comp1
