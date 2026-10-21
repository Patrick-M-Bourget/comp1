#pragma once

// The Dry path and the Dry/Wet sum. The input is copied as the Dry path before the Wet path touches it, delayed by
// exactly what the Wet path delays it by up to the sum (comp1/Latency.h's wetPathLatencySamples), then summed with
// the Wet path as Dry × Dry level + Wet × Wet level. Both levels glide with plugkit's Smoother, a sample at a time,
// at the sum, so a Dry change and a Wet change from the same host block are heard at the same output sample, and a
// change, Host Automation included, is click-free and the same at every host block size. Settled at Off (-inf dB) a
// path contributes exactly nothing; settled at 0 dB it passes bit for bit, so with no compression Dry plus Wet nulls
// exactly against the input.

#include "DelayLine.h"
#include "Glide.h"
#include "comp1/Engine.h"
#include "comp1/Latency.h"

#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace comp1
{

class DryWet
{
public:
    // Allocates for up to numChannels channels of up to maxSamples samples a piece.
    void prepare (double sampleRate, int numChannels, int maxSamples)
    {
        for (auto* level : { &dry, &wet })
            level->configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        dry.reset (0.0);
        wet.reset (1.0);
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        storage.assign (channels, std::vector<float> (static_cast<std::size_t> (std::max (maxSamples, 0))));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        delay.prepare (numChannels, wetPathLatencySamples (ProcessingMode::HighQuality, sampleRate));
    }

    // Dry and Wet in dB, -inf for Off. With snap, they take effect at once instead of gliding.
    void setLevels (double dryDb, double wetDb, bool snap)
    {
        glideTo (dry, plugkit::decibelsToGain (dryDb), snap);
        glideTo (wet, plugkit::decibelsToGain (wetDb), snap);
    }

    // Copies input, a piece of at most the prepared channels and samples, as the Dry path, delayed as the Wet path is
    // on path: by High Quality's wetPathLatencySamples, or not at all in Zero Latency. The delay is fed in both
    // Processing Modes, so a switch to High Quality fades the Dry path in from the latest audio.
    void split (ConstAudioBlock input, ProcessingMode path)
    {
        const AudioBlock copy { pointers.data(), input.numChannels, input.numSamples };
        if (path == ProcessingMode::HighQuality)
        {
            delay.process (input, copy, delay.getLength());
            return;
        }
        delay.write (input);
        for (int ch = 0; ch < input.numChannels; ++ch)
            std::copy_n (input.channels[ch], input.numSamples, copy.channels[ch]);
    }

    // Sums the Dry path split from the same piece into the Wet path's piece, in place.
    void sum (AudioBlock audio)
    {
        // Wet at 0 dB and Dry at Off, settled: the Wet path plays as it is, bit for bit, with no work.
        if (wet.isSettled() && wet.value() == 1.0 && dry.isSettled() && dry.value() == 0.0)
            return;
        for (int i = 0; i < audio.numSamples; ++i)
        {
            const double d = dry.isSettled() ? dry.value() : dry.next();
            const double w = wet.isSettled() ? wet.value() : wet.next();
            for (int ch = 0; ch < audio.numChannels; ++ch)
            {
                float& sample = audio.channels[ch][i];
                sample = static_cast<float> (w * sample + d * pointers[static_cast<std::size_t> (ch)][i]);
            }
        }
    }

private:
    plugkit::Smoother dry, wet; // each a gain, 0 at Off
    std::vector<std::vector<float>> storage; // per channel, the Dry path's piece
    std::vector<float*> pointers;            // per channel, into storage
    DelayLine delay;                         // High Quality's, per channel
};

} // namespace comp1
