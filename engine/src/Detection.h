#pragma once

// The detection stage: what the detector hears, worked out apart from the audio the compressor applies gain to, so
// the audio can be delayed (Lookahead, #9) and the detection signal shaped (Detection Source #13, Detection EQ #14,
// Stereo Placement and Stereo Link #15) without touching the gain computer or the detector.

#include "comp1/Engine.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace comp1
{

// The detection signal for one piece of a run: its detection channels, and for each main channel the detection
// channel it follows. It points into the detection stage's own storage, never at the audio, and holds until the
// stage's next process().
struct DetectionSignal
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
    const int* followedBy = nullptr; // per main channel, the index of the detection channel it follows

    const float* channelFollowedBy (int mainChannel) const { return channels[followedBy[mainChannel]]; }
};

// Takes the Internal signal and the Sidechain (or none) for one piece of a run, each a block of that piece's samples,
// and returns the detection signal. It applies no gain. The Internal signal is whatever the caller passes: today the
// main input as the compressor receives it.
//
// With Detection Source Internal, the only one so far, there is one detection channel per main channel, carrying the
// Internal signal sample for sample, and each main channel follows its own. The Sidechain is accepted and unused until
// Detection Source External (#13).
class DetectionStage
{
public:
    // Allocates for up to numMainChannels channels of up to maxSamples samples a piece.
    void prepare (int numMainChannels, int maxSamples)
    {
        const auto channels = static_cast<std::size_t> (std::max (numMainChannels, 0));
        storage.assign (channels, std::vector<float> (static_cast<std::size_t> (std::max (maxSamples, 0))));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        followedBy.assign (channels, 0);
    }

    // internal holds at most the prepared channels and samples. Never allocates.
    DetectionSignal process (ConstAudioBlock internal, const ConstAudioBlock* /*sidechain*/)
    {
        assert (internal.numChannels <= static_cast<int> (storage.size()));
        assert (internal.numChannels == 0 || internal.numSamples <= static_cast<int> (storage[0].size()));
        for (int ch = 0; ch < internal.numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            std::copy_n (internal.channels[ch], internal.numSamples, storage[c].begin());
            followedBy[c] = ch;
        }
        return { .channels = pointers.data(),
                 .numChannels = internal.numChannels,
                 .numSamples = internal.numSamples,
                 .followedBy = followedBy.data() };
    }

private:
    std::vector<std::vector<float>> storage; // per detection channel
    std::vector<const float*> pointers;      // per detection channel, into storage
    std::vector<int> followedBy;             // per main channel
};

} // namespace comp1
