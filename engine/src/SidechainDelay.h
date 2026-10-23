#pragma once

// The Sidechain's piece delayed in High Quality by as much as the audio a stage listens beside has been, so External
// detection keeps the timing Internal detection has. Its delay is fed in both Processing Modes, with silence when
// nothing is connected, so a switch or a connection starts from the latest of it.

#include "DelayLine.h"
#include "comp1/Engine.h"

#include <plugkit/RunGrid.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace comp1
{

class SidechainDelay
{
public:
    // Allocates for a stereo Sidechain delayed by length samples, in pieces of up to a run.
    void prepare (int length)
    {
        delay.prepare (2, length);
        silence.assign (static_cast<std::size_t> (plugkit::runSamples), 0.0f);
        for (std::size_t ch = 0; ch < storage.size(); ++ch)
        {
            storage[ch].assign (static_cast<std::size_t> (plugkit::runSamples), 0.0f);
            pointers[ch] = storage[ch].data();
            constPointers[ch] = storage[ch].data();
        }
    }

    // The Sidechain's piece (or none) of numSamples samples as heard: in High Quality delayed by the length, in Zero
    // Latency as it is. What it returns holds until the next process().
    const ConstAudioBlock* process (const ConstAudioBlock* sidechainPiece, int numSamples, bool highQuality)
    {
        std::array<const float*, 2> in {};
        for (std::size_t ch = 0; ch < in.size(); ++ch)
            in[ch] = sidechainPiece == nullptr ? silence.data()
                                               : sidechainPiece->channels[std::min (static_cast<int> (ch), sidechainPiece->numChannels - 1)];
        if (! highQuality)
        {
            delay.write ({ in.data(), 2, numSamples });
            return sidechainPiece;
        }
        delay.process ({ in.data(), 2, numSamples }, { pointers.data(), 2, numSamples }, delay.getLength());
        if (sidechainPiece == nullptr)
            return nullptr;
        heard = { constPointers.data(), sidechainPiece->numChannels, numSamples };
        return &heard;
    }

private:
    DelayLine delay;
    std::array<std::vector<float>, 2> storage; // the Sidechain's piece, delayed, or silence
    std::array<float*, 2> pointers {};
    std::array<const float*, 2> constPointers {};
    ConstAudioBlock heard;
    std::vector<float> silence; // a run of it, for when nothing is connected
};

} // namespace comp1
