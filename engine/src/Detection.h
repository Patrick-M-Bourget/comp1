#pragma once

// The detection stage: what the detector hears, worked out apart from the audio the compressor applies gain to, so
// the audio can be delayed (Lookahead, #9) and the detection signal shaped (Detection EQ #14, Stereo Placement and
// Stereo Link #15) without touching the gain computer or the detector.

#include "comp1/Engine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

// The detection signal for one piece of a run: its detection channels, the signal Detection Audition plays, and for
// each main channel the samples it follows. A main channel follows a detection channel, except a mono main under a
// stereo detection signal, which follows the louder of the two at each sample. It points into the detection stage's
// own storage, never at the audio, and holds until the stage's next process().
struct DetectionSignal
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
    const float* const* followed = nullptr; // per main channel

    const float* channelFollowedBy (int mainChannel) const { return followed[mainChannel]; }
};

// Takes the Internal signal and the Sidechain (or none) for one piece of a run, each a block of that piece's samples,
// and returns the detection signal. It applies no gain. The Internal signal is whatever the caller passes: today the
// main input as the compressor receives it.
//
// With Detection Source Internal there is one detection channel per main channel, carrying the Internal signal sample
// for sample, and each main channel follows its own; the Sidechain changes nothing. With External the detection
// channels are the Sidechain's: each main channel follows the Sidechain channel of its own index, a mono Sidechain is
// followed by every main channel, so a mono key, like a kick, ducks the whole track, and a mono main follows the
// louder of a stereo Sidechain's two channels at each sample.
class DetectionStage
{
public:
    // Allocates for up to numMainChannels channels of up to maxSamples samples a piece, and a stereo Sidechain.
    void prepare (int numMainChannels, int maxSamples)
    {
        const auto mainChannels = static_cast<std::size_t> (std::max (numMainChannels, 0));
        const auto samples = static_cast<std::size_t> (std::max (maxSamples, 0));
        const auto channels = std::max (mainChannels, maxSidechainChannels);
        storage.assign (channels, std::vector<float> (samples));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        louder.assign (samples, 0.0f);
        followed.assign (mainChannels, nullptr);
    }

    void setSource (DetectionSource newSource) { source = newSource; }

    // internal holds at most the prepared channels and samples, and sidechain, when there is one, as many samples and
    // at most two channels. Never allocates.
    DetectionSignal process (ConstAudioBlock internal, const ConstAudioBlock* sidechain)
    {
        assert (internal.numChannels <= static_cast<int> (followed.size()));
        assert (internal.numChannels == 0 || internal.numSamples <= static_cast<int> (louder.size()));
        if (source == DetectionSource::External && sidechain != nullptr)
        {
            assert (sidechain->numChannels <= static_cast<int> (maxSidechainChannels) && sidechain->numSamples == internal.numSamples);
            for (int ch = 0; ch < sidechain->numChannels; ++ch)
                std::copy_n (sidechain->channels[ch], internal.numSamples, storage[static_cast<std::size_t> (ch)].begin());
            return signal (sidechain->numChannels, internal);
        }
        for (int ch = 0; ch < internal.numChannels; ++ch)
            std::copy_n (internal.channels[ch], internal.numSamples, storage[static_cast<std::size_t> (ch)].begin());
        return signal (internal.numChannels, internal);
    }

private:
    static constexpr std::size_t maxSidechainChannels = 2;

    // The detection signal of the first numChannels of storage, for the main channels of internal.
    DetectionSignal signal (int numChannels, ConstAudioBlock internal)
    {
        const bool takeLouder = internal.numChannels == 1 && numChannels == 2;
        if (takeLouder)
            for (int i = 0; i < internal.numSamples; ++i)
            {
                const auto s = static_cast<std::size_t> (i);
                louder[s] = std::abs (storage[1][s]) > std::abs (storage[0][s]) ? storage[1][s] : storage[0][s];
            }
        for (int ch = 0; ch < internal.numChannels; ++ch)
            followed[static_cast<std::size_t> (ch)] = takeLouder ? louder.data() : pointers[static_cast<std::size_t> (std::min (ch, numChannels - 1))];
        return { .channels = pointers.data(), .numChannels = numChannels, .numSamples = internal.numSamples, .followed = followed.data() };
    }

    DetectionSource source = DetectionSource::Internal;
    std::vector<std::vector<float>> storage; // per detection channel
    std::vector<const float*> pointers;      // per detection channel, into storage
    std::vector<float> louder;               // a mono main's, under a stereo detection signal
    std::vector<const float*> followed;      // per main channel, into storage or louder
};

} // namespace comp1
