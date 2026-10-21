#pragma once

// A Processing Mode switch, at the end of the chain: the output fades out, the paths swap in the silence, and the new
// one fades in. Never a crossfade between the two paths, whose delays differ and would comb-filter (ADR 0001).

#include "comp1/Engine.h"
#include "comp1/Latency.h"

#include <plugkit/Smoothstep.h>

#include <algorithm>
#include <climits>
#include <cmath>

namespace comp1
{

class ModeFade
{
public:
    // dyn-lab's MODE_FADE_MS, with its reasons: the fade-out, and the fade-in, each this long on a smoothstep curve.
    static constexpr double fadeMs = 10.0;

    void prepare (double sampleRate)
    {
        fadeSamples = std::max (1, static_cast<int> (std::lround (fadeMs * sampleRate / 1000.0)));
        // High Quality's delays after the compressor, the reserves' padding, hold the path left behind until they
        // have taken this many of the new path's samples, so the fade-in into High Quality waits for them in the
        // silence: it then rises from the new path's own audio. Its delays before the compressor are fed in both
        // modes, so they hold its audio already.
        highQualityFill = latencySamples (ProcessingMode::HighQuality, sampleRate) - lookaheadBudgetSamples (sampleRate);
        reset (path);
    }

    // At once, with no fade: mode plays from the next sample.
    void reset (ProcessingMode mode)
    {
        path = target = mode;
        position = fadeSamples;
        silence = 0;
    }

    // The Processing Mode to switch to, fading, when it isn't the one playing; a switch back while the old path is
    // still fading out fades it back in from where it is.
    void setTarget (ProcessingMode mode) { target = mode; }

    // The path that plays the next piece. At the start of a piece: the paths swap here once the fade-out has reached
    // silence.
    ProcessingMode startPiece()
    {
        if (target != path && position == 0)
        {
            path = target;
            silence = path == ProcessingMode::HighQuality ? highQualityFill : 0;
        }
        return path;
    }

    // How many samples the next piece may hold, so that the paths swap between pieces: up to the fade-out's last
    // sample, or any number.
    int pieceLimit() const { return target != path ? position : INT_MAX; }

    // Applies the fade to a piece of the path playing, a sample at a time, so it moves the same however the audio is
    // cut. Settled at full level, it leaves the audio as it is, bit for bit.
    void process (AudioBlock block)
    {
        for (int i = 0; i < block.numSamples; ++i)
        {
            if (target == path && silence == 0 && position == fadeSamples)
                return;
            if (target != path)
                --position;
            else if (silence > 0)
                --silence;
            else
                ++position;
            const auto gain = static_cast<float> (silence > 0 ? 0.0 : plugkit::smoothstep (static_cast<double> (position) / fadeSamples));
            for (int ch = 0; ch < block.numChannels; ++ch)
                block.channels[ch][i] *= gain;
        }
    }

private:
    int fadeSamples = 1;
    int highQualityFill = 0;
    ProcessingMode path = ProcessingMode::ZeroLatency, target = ProcessingMode::ZeroLatency;
    int position = 1; // along the fade, 0 (silence) to fadeSamples (full level)
    int silence = 0;  // samples of silence left before the fade-in
};

} // namespace comp1
