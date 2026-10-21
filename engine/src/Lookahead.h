#pragma once

// High Quality's Lookahead: the audio the compressor applies gain to is delayed by the whole Lookahead budget
// (comp1/Latency.h), and the detection signal by the budget less Lookahead, so the detector meets the audio Lookahead
// early. Moving Lookahead moves only the detection signal's delay, never the audio's, so the audio is never resampled.

#include "DelayLine.h"
#include "Detection.h"
#include "comp1/Latency.h"

#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

class LookaheadStage
{
public:
    // Allocates for up to numChannels audio channels, what each follows, and the Mid and Side, of up to maxSamples
    // samples a piece.
    void prepare (double newSampleRate, int numChannels, int maxSamples)
    {
        sampleRate = newSampleRate;
        budget = lookaheadBudgetSamples (sampleRate);
        audioDelay.prepare (numChannels, budget);
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0)) + 2;
        detectionDelay.prepare (static_cast<int> (channels), budget);
        inputs.assign (channels, nullptr);
        storage.assign (channels, std::vector<float> (static_cast<std::size_t> (std::max (maxSamples, 0))));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        // Close enough to count as arrived, in samples: far below the whole sample the delay is rounded to.
        lookahead.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-3);
    }

    // Lookahead in ms. It glides on the run grid, so a change, Host Automation included, is click-free and moves the
    // same at every host block size; with snap it takes effect at once.
    void setLookahead (double ms, bool snap)
    {
        const double samples = ms * sampleRate / 1000.0;
        if (snap)
            lookahead.reset (samples);
        else
            lookahead.setTarget (samples);
    }

    // At the start of each run: Lookahead moves on by a run.
    void startRun()
    {
        if (! lookahead.isSettled())
            lookahead.skip (plugkit::runSamples);
    }

    // Feeds the audio and what each of its channels follows in its detection signal, then the Mid and the Side, the
    // same piece of a run, into the delays, in both Processing Modes, so a switch to High Quality starts from the latest
    // audio. In High Quality, delays audio in place and returns the detection signal to compress it by, Lookahead ahead
    // of it: one channel per audio channel, each followed by its own, and its Mid and Side, with its place in the run
    // grid delayed as it is; in Zero Latency, leaves audio alone and returns detected. The detection signal returned holds until the next process().
    DetectionSignal process (const DetectionSignal& detected, AudioBlock audio, bool highQuality)
    {
        // What the compressor follows, a channel per audio channel: a stereo Sidechain under a mono main is followed
        // as the louder of its two, so its two channels can't be delayed in its place. Then the Mid and the Side.
        std::copy_n (detected.followed, audio.numChannels, inputs.begin());
        const auto parts = static_cast<std::size_t> (audio.numChannels);
        inputs[parts] = detected.mid != nullptr ? detected.mid : inputs.front();
        inputs[parts + 1] = detected.side != nullptr ? detected.side : inputs.front();
        const bool withParts = audio.numChannels > 0;
        const ConstAudioBlock detectedBlock { inputs.data(), withParts ? audio.numChannels + 2 : 0, detected.numSamples };
        if (! highQuality)
        {
            audioDelay.write ({ audio.channels, audio.numChannels, audio.numSamples });
            detectionDelay.write (detectedBlock);
            return detected;
        }
        audioDelay.process (audio);
        // The detection signal's delay is a whole number of samples, so the detector reads the detection signal's own
        // samples, never a level between two of them. While Lookahead glides, the delay steps by a sample or two a
        // run; the detector, smoothing with Attack and Release, makes no click of it.
        const int delay = std::clamp (budget - static_cast<int> (std::lround (lookahead.value())), 0, budget);
        detectionDelay.process (detectedBlock, { pointers.data(), detectedBlock.numChannels, detectedBlock.numSamples }, delay);
        return { .channels = pointers.data(),
                 .numChannels = audio.numChannels,
                 .numSamples = detectedBlock.numSamples,
                 .followed = pointers.data(),
                 .mid = withParts ? pointers[parts] : nullptr,
                 .side = withParts ? pointers[parts + 1] : nullptr,
                 .runPosition = ((detected.runPosition - delay) % plugkit::runSamples + plugkit::runSamples) % plugkit::runSamples };
    }

private:
    double sampleRate = 44100.0;
    int budget = 0; // the Lookahead budget, in samples
    DelayLine audioDelay, detectionDelay;
    plugkit::Smoother lookahead;             // in samples
    std::vector<const float*> inputs;        // per audio channel what it follows, then the Mid and the Side
    std::vector<std::vector<float>> storage; // the same, delayed
    std::vector<float*> pointers;            // into storage
};

} // namespace comp1
