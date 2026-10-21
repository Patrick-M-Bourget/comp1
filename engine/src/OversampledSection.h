#pragma once

// One of High Quality's two oversampled sections on the Wet path (ADR 0009): a nonlinear stage's linear part, the
// section's input, delayed by exactly oversamplingSectionSamples, plus its residual worked out at the Oversampling in
// force, through the up filters, the stage and the down filters, and padded to the same delay. So the section's delay
// never depends on the factor or on the stage, and with the stage adding nothing it is a plain delay, bit for bit.

#include "ModeFade.h"
#include "Oversampler.h"
#include "comp1/Engine.h"
#include "comp1/Latency.h"

#include <plugkit/Smoothstep.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

// An Oversampling change fades the residual's weight out over ModeFade's fadeMs on a smoothstep curve, changes the
// filters while it is silent, feeds the new ones for a section's length so the residual comes from their settled
// output, then fades the weight back in over fadeMs. The linear part plays on throughout, so with the stage adding
// nothing the output never changes. The Oversampling in force at prepare() (snap) applies at once.
class OversampledSection
{
public:
    // Allocates for up to numChannels channels of up to maxSamples a piece, at up to the largest Oversampling allowed
    // at sampleRate.
    void prepare (double sampleRate, int numChannels, int maxSamples)
    {
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        largest = maxOversampling (sampleRate);
        pieceSamples = std::max (maxSamples, 0);
        filters.assign (channels, {});
        for (auto& channel : filters)
            channel.prepare (largest, pieceSamples);
        const auto section = static_cast<std::size_t> (oversamplingSectionSamples);
        linear.assign (channels, std::vector<float> (section));
        residualDelay.assign (channels, std::vector<double> (section));
        oversampled.assign (channels, std::vector<double> (static_cast<std::size_t> (pieceSamples * factorOf (largest))));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = oversampled[ch].data();
        down.assign (static_cast<std::size_t> (pieceSamples), 0.0);
        weights.assign (static_cast<std::size_t> (pieceSamples), 1.0);
        primer.assign (section, 0.0f);
        fadeSamples = std::max (1, static_cast<int> (std::lround (ModeFade::fadeMs * sampleRate / 1000.0)));
        linearPosition = 0;
        wanted = Oversampling::x1;
        change (Oversampling::x1);
    }

    // The Oversampling in force, at most the largest prepared; with snap, it applies at once instead of fading.
    void setOversampling (Oversampling inForce, bool snap)
    {
        wanted = std::min (inForce, largest);
        if (snap && wanted != current)
            change (wanted);
    }

    // At the start of each run of the Engine's grid, so a change takes effect at the same sample however the host cuts
    // the audio: a change faded out takes effect, held silent for a section while the new filters settle. In Zero
    // Latency no residual is heard, so it takes effect at once, at full weight.
    void startRun()
    {
        if (wanted == current)
            return;
        if (writing)
            change (wanted);
        else if (fadePosition == 0)
        {
            change (wanted);
            fadePosition = 0;
            hold = oversamplingSectionSamples;
        }
    }

    // Zero Latency: feeds the section's input into its linear delay, without reading any out, so the delay holds the
    // latest audio when High Quality reads it again. The filters start afresh when High Quality runs the section
    // again.
    void write (ConstAudioBlock audio)
    {
        writing = true;
        cold = true;
        for (int i = 0; i < audio.numSamples; ++i)
        {
            for (int ch = 0; ch < audio.numChannels; ++ch)
                linear[static_cast<std::size_t> (ch)][static_cast<std::size_t> (linearPosition)] = audio.channels[ch][i];
            linearPosition = linearPosition + 1 == oversamplingSectionSamples ? 0 : linearPosition + 1;
        }
    }

    // High Quality: the section on audio, in place, a piece of at most the prepared samples. residual (host,
    // oversampled, factor) is the nonlinear stage: it replaces oversampled, host's samples at factor samples each,
    // with its residual and returns true, or adds nothing and returns false. heard says whether it will add one, so a
    // silent stage costs no filtering.
    template <typename Residual>
    void process (AudioBlock audio, bool heard, Residual&& residual)
    {
        writing = false;
        for (int i = 0; i < audio.numSamples; ++i)
            weights[static_cast<std::size_t> (i)] = nextWeight();
        const ConstAudioBlock host { audio.channels, audio.numChannels, audio.numSamples };
        bool added = false;
        if (heard)
        {
            if (cold)
                warm (audio.numChannels);
            for (int ch = 0; ch < audio.numChannels; ++ch)
                filters[static_cast<std::size_t> (ch)].up (audio.channels[ch], audio.numSamples, pointers[static_cast<std::size_t> (ch)]);
        }
        added = residual (host, pointers.data(), factorOf (current)) && heard;
        // Filters left unfed start afresh when the stage is heard again.
        cold = ! added;
        const int padding = oversamplingSectionSamples - oversampling::pairDelaySamples (current);
        int residualAt = residualPosition;
        for (int ch = 0; ch < audio.numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            if (added)
                filters[c].down (pointers[c], audio.numSamples, down.data());
            else
                // -0.0 adds nothing to any sample, -0.0 included.
                std::fill_n (down.begin(), audio.numSamples, -0.0);
            int position = linearPosition;
            residualAt = residualPosition;
            float* samples = audio.channels[ch];
            auto& line = linear[c];
            auto& pad = residualDelay[c];
            for (int i = 0; i < audio.numSamples; ++i)
            {
                const auto n = static_cast<std::size_t> (i);
                const double x = line[static_cast<std::size_t> (position)];
                line[static_cast<std::size_t> (position)] = samples[i];
                position = position + 1 == oversamplingSectionSamples ? 0 : position + 1;
                double r = down[n];
                if (padding > 0)
                {
                    std::swap (r, pad[static_cast<std::size_t> (residualAt)]);
                    residualAt = residualAt + 1 == padding ? 0 : residualAt + 1;
                }
                // Whole, the residual is added as it is, so at 1x the sample is exactly what Zero Latency's stage
                // makes of it; silent, the linear part plays alone.
                const double weight = weights[n];
                samples[i] = weight == 1.0   ? static_cast<float> (x + r)
                             : weight == 0.0 ? static_cast<float> (x)
                                             : static_cast<float> (x + weight * r);
            }
        }
        residualPosition = residualAt;
        linearPosition = (linearPosition + audio.numSamples) % oversamplingSectionSamples;
    }

private:
    // The residual's next weight: fading out toward a change, held silent after one, fading in, or whole. A change
    // back while fading out fades in again from where it is.
    double nextWeight()
    {
        if (wanted != current)
        {
            if (fadePosition > 0)
                --fadePosition;
        }
        else if (hold > 0)
        {
            --hold;
            return 0.0;
        }
        else if (fadePosition < fadeSamples)
            ++fadePosition;
        return fadePosition == fadeSamples ? 1.0 : plugkit::smoothstep (static_cast<double> (fadePosition) / fadeSamples);
    }

    // Takes oversampling at once, at full weight: its filters from silence, and the residual's padding holding
    // nothing.
    void change (Oversampling oversampling)
    {
        current = oversampling;
        fadePosition = fadeSamples;
        hold = 0;
        for (auto& channel : filters)
            channel.setOversampling (current);
        for (auto& pad : residualDelay)
            std::fill (pad.begin(), pad.end(), -0.0);
        residualPosition = 0;
        cold = true;
    }

    // Filters starting afresh: fed the linear delay's samples, the last section of the input, so the up filters, whose
    // history is shorter than a section, hold what they would had they run all along.
    void warm (int numChannels)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            filters[c].reset();
            const auto& line = linear[c];
            for (int i = 0; i < oversamplingSectionSamples; ++i)
                primer[static_cast<std::size_t> (i)] = line[static_cast<std::size_t> ((linearPosition + i) % oversamplingSectionSamples)];
            for (int start = 0; start < oversamplingSectionSamples; start += pieceSamples)
                filters[c].up (primer.data() + start, std::min (pieceSamples, oversamplingSectionSamples - start), pointers[c]);
        }
        cold = false;
    }

    Oversampling largest = Oversampling::x1, current = Oversampling::x1, wanted = Oversampling::x1;
    int pieceSamples = 0;
    std::vector<oversampling::Oversampler> filters; // per channel
    std::vector<std::vector<float>> linear;         // per channel, the linear part's delay, a section long
    int linearPosition = 0;                         // where the next sample goes in it, the same on every channel
    std::vector<std::vector<double>> residualDelay; // per channel, the residual's padding up to the section
    int residualPosition = 0;                       // likewise
    std::vector<std::vector<double>> oversampled;   // per channel, a piece at the oversampled rate
    std::vector<double*> pointers;                  // into oversampled
    std::vector<double> down;                       // a channel's residual, back at the host rate
    std::vector<double> weights;                    // the residual's weight at each sample of a piece
    std::vector<float> primer;                      // a channel's linear delay, oldest first
    bool cold = true;                               // the filters start afresh at the next residual
    bool writing = false;                           // Zero Latency's: last fed by write()
    int fadeSamples = 1;
    int fadePosition = 1; // along the fade, 0 (silent) to fadeSamples (whole)
    int hold = 0;         // samples left to hold the residual silent before it fades in
};

} // namespace comp1
