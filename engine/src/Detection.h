#pragma once

// The detection stage: what the detector hears, worked out apart from the audio the compressor applies gain to, so
// the audio can be delayed (High Quality's Lookahead, Lookahead.h) and the detection signal shaped (Detection Source
// #13, Detection EQ #14, Stereo Placement) without touching the gain computer or the detector.

#include "comp1/Engine.h"

#include <plugkit/Placement.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

namespace comp1
{

// The detection signal for one piece of a run: its detection channels, which part of the signal each drives, and for
// each main channel the detection channel it follows under Stereo Placement Stereo. It points into the detection
// stage's own storage, never at the audio, and holds until the stage's next process().
struct DetectionSignal
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
    const int* followedBy = nullptr; // per main channel, the index of the detection channel it follows under Stereo
    int midChannel = 0;              // the index of the detection channel Stereo Placement Mid compresses by
    int sideChannel = 0;             // and Side's

    const float* channelFollowedBy (int mainChannel) const { return channels[followedBy[mainChannel]]; }
    const float* mid() const { return channels[midChannel]; }
    const float* side() const { return channels[sideChannel]; }
};

// Takes the Internal signal and the Sidechain (or none) for one piece of a run, each a block of that piece's samples,
// and returns the detection signal. It applies no gain. The Internal signal is whatever the caller passes: today the
// main input as the compressor receives it.
//
// With Detection Source Internal, the only one so far, the signal it routes is the Internal signal; the Sidechain is
// accepted and unused until Detection Source External (#13). Stereo Placement routing comes last, through plugkit's
// routePlacement and encoders, so every site that encodes Mid and Side encodes them the same way.
//
// A stereo signal gives four detection channels: its own two, each main channel following its own, then its Mid and
// its Side. All four are there whichever Placement plays, so a Placement fading in, or heard through Lookahead's delay,
// hears its part from the first sample. A mono signal gives one detection channel, carrying it sample for sample,
// which every main channel follows and which is the Mid and the Side too: a mono detection signal drives every
// Placement, Side included.
class DetectionStage
{
public:
    // The most detection channels the stage gives: a stereo signal's two, its Mid and its Side.
    static constexpr int maxChannels = 4;

    // Allocates for up to numMainChannels main channels, one or two, of up to maxSamples samples a piece.
    void prepare (int numMainChannels, int maxSamples)
    {
        for (std::size_t ch = 0; ch < storage.size(); ++ch)
        {
            storage[ch].assign (static_cast<std::size_t> (std::max (maxSamples, 0)), 0.0f);
            pointers[ch] = storage[ch].data();
        }
        followedBy.assign (static_cast<std::size_t> (std::clamp (numMainChannels, 0, 2)), 0);
    }

    // internal holds at most the prepared channels and samples. Never allocates.
    DetectionSignal process (ConstAudioBlock internal, const ConstAudioBlock* /*sidechain*/)
    {
        assert (internal.numChannels <= static_cast<int> (followedBy.size()));
        assert (internal.numSamples <= static_cast<int> (storage[0].size()));
        return route (internal);
    }

private:
    // Routes signal, of one or two channels, by every Placement.
    DetectionSignal route (ConstAudioBlock signal)
    {
        DetectionSignal routed { .channels = pointers.data(), .numSamples = signal.numSamples, .followedBy = followedBy.data() };
        const auto copy = [&] (int part, int source) {
            std::copy_n (signal.channels[source], signal.numSamples, storage[static_cast<std::size_t> (part)].begin());
        };
        routed.numChannels = plugkit::routePlacement (plugkit::StereoPlacement::Stereo, signal.numChannels, copy, [] (bool) {});
        for (std::size_t ch = 0; ch < followedBy.size(); ++ch)
            followedBy[ch] = routed.numChannels > 1 ? static_cast<int> (ch) : 0;
        if (signal.numChannels < 2)
            return routed;

        const auto encode = [&] (bool mid) {
            auto& part = storage[static_cast<std::size_t> (routed.numChannels)];
            (mid ? routed.midChannel : routed.sideChannel) = routed.numChannels++;
            for (int i = 0; i < signal.numSamples; ++i)
                part[static_cast<std::size_t> (i)] = mid ? plugkit::encodeMid (signal.channels[0][i], signal.channels[1][i])
                                                         : plugkit::encodeSide (signal.channels[0][i], signal.channels[1][i]);
        };
        for (const auto placement : { plugkit::StereoPlacement::Mid, plugkit::StereoPlacement::Side })
            plugkit::routePlacement (placement, signal.numChannels, [] (int, int) {}, encode);
        return routed;
    }

    std::array<std::vector<float>, maxChannels> storage; // per detection channel
    std::array<const float*, maxChannels> pointers {};    // per detection channel, into storage
    std::vector<int> followedBy;                          // per main channel
};

} // namespace comp1
