#pragma once

// The Detection EQ: a high-pass, a low-pass and one free band, filtering the detection source's channels inside the
// detection stage (Detection.h), so the detector hears them shaped and the processed sound is never touched.

#include "comp1/Settings.h"

#include <plugkit/Biquad.h>
#include <plugkit/RunGrid.h>
#include <plugkit/ShapeDesign.h>
#include <plugkit/Smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

// One Detection EQ band's filter on up to two channels, run on plugkit's matched designs.
//
// Frequency, Gain and Q glide with plugkit's Smoother, a run at a time on the 16-sample run grid: while one moves, the
// cascade is redesigned at each run's start and the coefficients glide from the last design to the new one across the
// run, sample by sample, with plugkit's interpolate. Frequency and Q glide in log space, as their host parameters move.
// A change of structure (Shape, a Slope that changes the order, the band switched on or off) can't glide, since the
// sections differ: the new filter starts from silence beside the old one, and the two crossfade over the glide time.
// A structure that changes again during a crossfade waits for it to finish. A band that is off and settled costs
// nothing, and passes its input bit for bit.
class DetectionEqBand
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const double timeConstant = plugkit::glideTimeConstantSeconds * sampleRate;
        for (auto& glide : glides)
            glide.configure (timeConstant, arrivalTolerance);
        fade.configure (timeConstant, 1.0e-6);
    }

    // With snap, the band takes the settings at once instead of gliding or crossfading.
    void setTarget (const DetectionBand& band, bool snap)
    {
        target = band;
        const double values[] = { std::log (band.filter.frequency), band.filter.gain, std::log (band.filter.q) };
        for (std::size_t g = 0; g < count; ++g)
        {
            if (snap)
                glides[g].reset (values[g]);
            else
                glides[g].setTarget (values[g]);
        }
        if (snap)
        {
            current = { .on = band.on, .structure = plugkit::structureOf (band.filter) };
            outgoing = {};
            fade.reset (1.0);
            fading = gliding = false;
            current.cascade = current.on ? design() : plugkit::Cascade {};
            from = current.cascade;
        }
    }

    // At each run's start: the glides move on by a run and the cascade is redesigned to where they now stand, or a
    // crossfade to a new structure starts.
    void startRun()
    {
        from = current.cascade;
        if (fading && fade.isSettled())
        {
            fading = false;
            outgoing.on = false;
            outgoing.cascade = {};
        }
        bool moving = false;
        for (auto& glide : glides)
            if (! glide.isSettled())
            {
                glide.skip (plugkit::runSamples);
                moving = true;
            }

        const Filter next { .on = target.on, .structure = plugkit::structureOf (target.filter) };
        if (! fading && (next.on != current.on || (next.on && next.structure != current.structure)))
        {
            // The filter playing now fades out as it stands; the new one fades in from silence.
            outgoing = current;
            current = next;
            current.clear();
            current.cascade = current.on ? design() : plugkit::Cascade {};
            from = current.cascade;
            fade.reset (0.0);
            fade.setTarget (1.0);
            fading = true;
            gliding = false;
            return;
        }
        gliding = moving && current.on;
        if (gliding)
            current.cascade = design();
    }

    // Filters the first numSamples of each of channels in place: a piece of a run, starting at runPosition.
    void process (float* const* channels, int numChannels, int numSamples, int runPosition)
    {
        if (! fading && ! current.on)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            const int n = runPosition + i;
            const auto coefficients = gliding ? plugkit::interpolate (from, current.cascade, (n + 1.0) / plugkit::runSamples) : current.cascade;
            const double m = fading ? fade.next() : 1.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = channels[ch][i];
                const double x = sample;
                const double in = current.run (coefficients, ch, x);
                const double y = fading ? (1.0 - m) * outgoing.run (outgoing.cascade, ch, x) + m * in : in;
                sample = static_cast<float> (y);
            }
        }
    }

private:
    // A filter of one structure: its sections' coefficients, and each channel's state through them.
    struct Filter
    {
        bool on = false;
        plugkit::Structure structure;
        plugkit::Cascade cascade;
        std::array<std::array<plugkit::BiquadState, plugkit::maxSections>, maxChannels> state {};

        void clear() { state = {}; }

        // One sample of channel ch through coefficients, in double from section to section.
        double run (const plugkit::Cascade& coefficients, int ch, double x)
        {
            auto& channelState = state[static_cast<std::size_t> (ch)];
            for (int s = 0; s < coefficients.count; ++s)
            {
                const auto& c = coefficients.sections[static_cast<std::size_t> (s)];
                auto& st = channelState[static_cast<std::size_t> (s)];
                x = plugkit::biquadStep (c.b0, c.b1, c.b2, c.a1, c.a2, x, st.s1, st.s2);
            }
            return x;
        }
    };

    enum Glide : std::size_t { logFrequency, gain, logQ, count };
    // Close enough to count as arrived, in log Hz, dB and log Q: far below what can be heard.
    static constexpr double arrivalTolerance = 1.0e-7;

    // The target's filter, at the glides' values.
    plugkit::Cascade design() const
    {
        auto filter = target.filter;
        filter.frequency = std::exp (glides[logFrequency].value());
        filter.gain = glides[gain].value();
        filter.q = std::exp (glides[logQ].value());
        return plugkit::designBand (filter, sampleRate);
    }

    double sampleRate = 48000.0;
    DetectionBand target;
    plugkit::Smoother glides[count];
    plugkit::Smoother fade; // 0 plays the outgoing filter, 1 the current one
    Filter current, outgoing;
    plugkit::Cascade from; // the design the coefficients glide from across this run
    bool gliding = false, fading = false;
};

// The three Detection EQ bands in turn: high-pass, low-pass, free band. It keeps its own place in the 16-sample run
// grid: it is fed the pieces of each run in order, from the grid's start at prepare(), and no piece crosses a run's
// end.
class DetectionEqStage
{
public:
    void prepare (double sampleRate)
    {
        for (auto& band : bands)
            band.prepare (sampleRate);
        runPosition = 0;
    }

    void setTarget (const DetectionEq& eq, bool snap)
    {
        bands[0].setTarget (eq.highPass, snap);
        bands[1].setTarget (eq.lowPass, snap);
        bands[2].setTarget (eq.band, snap);
    }

    void process (float* const* channels, int numChannels, int numSamples)
    {
        if (runPosition == 0)
            for (auto& band : bands)
                band.startRun();
        for (auto& band : bands)
            band.process (channels, std::min (numChannels, DetectionEqBand::maxChannels), numSamples, runPosition);
        runPosition += numSamples;
        if (runPosition >= plugkit::runSamples)
            runPosition = 0;
    }

private:
    std::array<DetectionEqBand, 3> bands;
    int runPosition = 0;
};

} // namespace comp1
