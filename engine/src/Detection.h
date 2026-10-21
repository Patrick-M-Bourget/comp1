#pragma once

// The detection stage: what the detector hears, worked out apart from the audio the compressor applies gain to, so
// the audio can be delayed (High Quality's Lookahead, Lookahead.h) and the detection signal shaped (the Detection EQ,
// DetectionEq.h, then Stereo Placement) without touching the gain computer or the detector.

#include "DetectionEq.h"
#include "comp1/Engine.h"

#include <plugkit/Placement.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

// The detection signal for one piece of a run: its detection channels, the signal Detection Audition plays, and for
// each main channel the samples it follows. A main channel follows a detection channel, except a mono main under a
// stereo detection signal, which follows the louder of the two at each sample. Stereo Placement Mid and Side follow the
// detection signal's Mid and Side: a stereo one's, encoded from its two channels, or a mono one itself, so a mono
// detection signal drives every Placement, Side included. It points into the detection stage's
// own storage, never at the audio, and holds until the stage's next process().
//
// It carries its own place in the run grid: where its first sample fell in the grid as the detection stage took it
// in. A delay of the detection signal (High Quality's Lookahead, Lookahead.h) delays that place with it, so what runs
// on the grid as the detector hears it (Auto Release) runs on the same samples in every Processing Mode.
struct DetectionSignal
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
    const float* const* followed = nullptr; // per main channel, under Stereo Placement Stereo
    const float* mid = nullptr;             // what Stereo Placement Mid follows
    const float* side = nullptr;            // and Side
    int runPosition = 0;                    // where the first sample fell in the run grid, 0 to a run less one

    const float* channelFollowedBy (int mainChannel) const { return followed[mainChannel]; }
};

// Takes the Internal signal and the Sidechain (or none) for one piece of a run, each a block of that piece's samples,
// and returns the detection signal. It applies no gain. The Internal signal is whatever the caller passes: today the
// main input as the compressor receives it.
//
// With Detection Source Internal there is one detection channel per main channel, carrying the Internal signal sample
// for sample, and each main channel follows its own; the Sidechain changes nothing. With External the detection
// channels are the Sidechain's: each main channel follows the Sidechain channel of its own index, a mono Sidechain is
// followed by every main channel, so a mono Sidechain, like a kick, ducks the whole track, and a mono main follows the
// louder of a stereo Sidechain's two channels at each sample. With External and no Sidechain, the detection signal is
// silence, a channel of it per main channel: no Gain Reduction starts, and any in progress returns at Release.
//
// The Detection EQ (DetectionEq.h) filters the detection source's channels, the Internal signal's or the
// Sidechain's, before the stage routes them, so its filters run on without a break when Detection Source changes.
// The stage keeps its own place in the 16-sample run grid for it: it is fed the pieces of each run in order, from
// the grid's start at prepare(), and no piece crosses a run's end.
class DetectionStage
{
public:
    // Allocates for up to numMainChannels channels of up to maxSamples samples a piece, and a stereo Sidechain.
    void prepare (double sampleRate, int numMainChannels, int maxSamples)
    {
        eq.prepare (sampleRate);
        const auto mainChannels = static_cast<std::size_t> (std::max (numMainChannels, 0));
        const auto samples = static_cast<std::size_t> (std::max (maxSamples, 0));
        const auto channels = std::max (mainChannels, maxSidechainChannels);
        storage.assign (channels, std::vector<float> (samples));
        pointers.assign (channels, nullptr);
        writable.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = writable[ch] = storage[ch].data();
        louder.assign (samples, 0.0f);
        for (auto& part : parts)
            part.assign (samples, 0.0f);
        followed.assign (mainChannels, nullptr);
    }

    void setSource (DetectionSource newSource) { source = newSource; }

    // With snap, the Detection EQ takes the settings at once instead of gliding.
    void setDetectionEq (const DetectionEq& settings, bool snap) { eq.setTarget (settings, snap); }

    // internal holds at most the prepared channels and samples, and sidechain, when there is one, as many samples and
    // at most two channels. Never allocates.
    DetectionSignal process (ConstAudioBlock internal, const ConstAudioBlock* sidechain)
    {
        assert (internal.numChannels <= static_cast<int> (followed.size()));
        assert (internal.numChannels == 0 || internal.numSamples <= static_cast<int> (louder.size()));
        if (source == DetectionSource::External)
        {
            // With no Sidechain connected the detector hears silence, a channel of it per main channel.
            if (sidechain == nullptr)
            {
                for (int ch = 0; ch < internal.numChannels; ++ch)
                    std::fill_n (storage[static_cast<std::size_t> (ch)].begin(), internal.numSamples, 0.0f);
                return filtered (internal.numChannels, internal);
            }
            assert (sidechain->numChannels <= static_cast<int> (maxSidechainChannels) && sidechain->numSamples == internal.numSamples);
            // A non-finite Sidechain sample (nan, inf) is heard as silence, as a main input one is: it would hold the
            // detector's Gain Reduction forever.
            for (int ch = 0; ch < sidechain->numChannels; ++ch)
                std::transform (sidechain->channels[ch], sidechain->channels[ch] + internal.numSamples,
                                storage[static_cast<std::size_t> (ch)].begin(),
                                [] (float s) { return std::isfinite (s) ? s : 0.0f; });
            return filtered (sidechain->numChannels, internal);
        }
        for (int ch = 0; ch < internal.numChannels; ++ch)
            std::copy_n (internal.channels[ch], internal.numSamples, storage[static_cast<std::size_t> (ch)].begin());
        return filtered (internal.numChannels, internal);
    }

private:
    static constexpr std::size_t maxSidechainChannels = 2;

    // The first numChannels of storage through the Detection EQ, then routed by signal().
    DetectionSignal filtered (int numChannels, ConstAudioBlock internal)
    {
        const int runPosition = eq.runPosition();
        eq.process (writable.data(), numChannels, internal.numSamples);
        auto routed = signal (numChannels, internal);
        routed.runPosition = runPosition;
        return routed;
    }

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
        DetectionSignal routed { .channels = pointers.data(), .numChannels = numChannels, .numSamples = internal.numSamples, .followed = followed.data() };
        routeMidAndSide (routed);
        return routed;
    }

    // Stereo Placement routing, last, through plugkit's routePlacement and encoders, so every site that encodes the Mid
    // and the Side encodes them the same way. Both are worked out whichever Placement plays, so a Placement fading in,
    // or heard through Lookahead's delay, hears its part from the first sample.
    void routeMidAndSide (DetectionSignal& routed)
    {
        if (routed.numChannels < 2)
        {
            routed.mid = routed.side = routed.numChannels == 1 ? routed.channels[0] : nullptr;
            return;
        }
        const float* left = routed.channels[0];
        const float* right = routed.channels[1];
        const auto encode = [&] (bool mid) {
            auto& part = parts[mid ? 0 : 1];
            for (int i = 0; i < routed.numSamples; ++i)
            {
                const auto s = static_cast<std::size_t> (i);
                part[s] = mid ? plugkit::encodeMid (left[s], right[s]) : plugkit::encodeSide (left[s], right[s]);
            }
        };
        for (const auto placement : { plugkit::StereoPlacement::Mid, plugkit::StereoPlacement::Side })
            plugkit::routePlacement (placement, routed.numChannels, [] (int, int) {}, encode);
        routed.mid = parts[0].data();
        routed.side = parts[1].data();
    }

    DetectionSource source = DetectionSource::Internal;
    DetectionEqStage eq;
    std::vector<std::vector<float>> storage; // per detection channel
    std::vector<const float*> pointers;      // per detection channel, into storage
    std::vector<float*> writable;            // the same, for the Detection EQ to filter in place
    std::vector<float> louder;               // a mono main's, under a stereo detection signal
    std::vector<const float*> followed;      // per main channel, into storage or louder
    std::array<std::vector<float>, 2> parts; // a stereo detection signal's Mid and Side
};

} // namespace comp1
