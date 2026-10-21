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
    // Allocates for up to numChannels audio channels, and as many detection channels as the detection stage gives, of
    // up to maxSamples samples a piece.
    void prepare (double newSampleRate, int numChannels, int maxSamples)
    {
        sampleRate = newSampleRate;
        budget = lookaheadBudgetSamples (sampleRate);
        audioDelay.prepare (numChannels, budget);
        detectionDelay.prepare (DetectionStage::maxChannels, budget);
        const auto channels = static_cast<std::size_t> (DetectionStage::maxChannels);
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

    // Feeds the audio and its detection signal, the same piece of a run, into the delays, in both Processing Modes, so
    // a switch to High Quality starts from the latest audio. In High Quality, delays audio in place and returns the
    // detection signal to compress it by, Lookahead ahead of it; in Zero Latency, leaves audio alone and returns
    // detected. The detection signal returned holds until the next process().
    DetectionSignal process (const DetectionSignal& detected, AudioBlock audio, bool highQuality)
    {
        const ConstAudioBlock detectedBlock { detected.channels, detected.numChannels, detected.numSamples };
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
        detectionDelay.process (detectedBlock, { pointers.data(), detected.numChannels, detected.numSamples }, delay);
        return { .channels = pointers.data(),
                 .numChannels = detected.numChannels,
                 .numSamples = detected.numSamples,
                 .followedBy = detected.followedBy,
                 .midChannel = detected.midChannel,
                 .sideChannel = detected.sideChannel };
    }

private:
    double sampleRate = 44100.0;
    int budget = 0; // the Lookahead budget, in samples
    DelayLine audioDelay, detectionDelay;
    plugkit::Smoother lookahead;             // in samples
    std::vector<std::vector<float>> storage; // per detection channel, the delayed detection signal
    std::vector<float*> pointers;            // per detection channel, into storage
};

} // namespace comp1
