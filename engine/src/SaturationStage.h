#pragma once

// The Saturation stage on the Wet path: the saturation module (Saturation.h) driven by Drive, level-compensated, at
// its Saturation Position, Before or After the compressor.

#include "Saturation.h"

#include "comp1/Engine.h"

#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace comp1
{

// Each sample x becomes x + w r(g x) / g, with g the Drive's gain and r the residual anti-aliased (ADR 0005), so the
// linear part passes sample-aligned, adding no delay. w is the residual's weight, from 0 to 1.
//
// Saturation on and off glides w with plugkit's Smoother, a sample at a time. Settled at exactly 0, the stage leaves
// the audio as it is, bit for bit, while each channel's ADAA state follows the driven signal, so switching on has no
// transient. Drive glides in dB, a sample at a time too: it shapes the waveform directly, so a step of a whole run,
// as the compressor's settings take, would put a corner in it every run. A Saturation Position change fades w out,
// moves the stage at the start of a run once w is exactly 0, and fades w back in: the move is click-free by
// construction, and two nonlinear paths are never crossfaded. Where the host's blocks fall changes nothing.
class SaturationStage
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        channels.assign (static_cast<std::size_t> (std::max (numChannels, 0)), {});
        weight.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        drive.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
    }

    // With snap, the settings take effect at once instead of gliding, the position included.
    void setSettings (const Settings& settings, bool snap)
    {
        on = settings.saturation;
        wanted = settings.saturationPosition;
        if (snap)
        {
            position = wanted;
            drive.reset (settings.driveDb);
            gain = plugkit::decibelsToGain (settings.driveDb);
            weight.reset (on ? 1.0 : 0.0);
        }
        else
        {
            drive.setTarget (settings.driveDb);
            aimWeight();
        }
    }

    // At the start of each run: a stage faded out moves to its new position, and fades back in.
    void startRun()
    {
        if (position != wanted && silent())
        {
            position = wanted;
            arrived = true;
        }
        aimWeight();
    }

    // Saturates audio in place when the stage is at, and otherwise leaves it alone.
    void process (SaturationPosition at, AudioBlock audio)
    {
        if (at != position || audio.numSamples == 0)
            return;
        // Just moved: each channel's ADAA starts from the signal here, not the one at the old position.
        if (arrived)
        {
            for (int ch = 0; ch < audio.numChannels; ++ch)
                channels[static_cast<std::size_t> (ch)].follow (gain * audio.channels[ch][0]);
            arrived = false;
        }
        if (silent())
        {
            for (int i = 0; i < audio.numSamples && ! drive.isSettled(); ++i)
                glideDrive();
            for (int ch = 0; ch < audio.numChannels; ++ch)
                channels[static_cast<std::size_t> (ch)].follow (gain * audio.channels[ch][audio.numSamples - 1]);
            return;
        }
        // The weight and Drive glide a sample at a time; once both have settled, next() would return each goal
        // exactly, so the rest takes them as constants, sample for sample the same.
        int i = 0;
        for (; i < audio.numSamples && ! (weight.isSettled() && drive.isSettled()); ++i)
        {
            if (! drive.isSettled())
                glideDrive();
            saturate (audio, i, weight.next());
        }
        const double w = weight.value();
        for (; i < audio.numSamples; ++i)
            saturate (audio, i, w);
    }

private:
    // Close enough to count as arrived, in the weight's units and in dB: far below what can be heard.
    static constexpr double arrivalTolerance = 1.0e-7;

    // Faded out exactly: the stage changes nothing.
    bool silent() const { return weight.isSettled() && weight.value() == 0.0; }

    void aimWeight() { weight.setTarget (on && position == wanted ? 1.0 : 0.0); }

    void glideDrive() { gain = plugkit::decibelsToGain (drive.next()); }

    void saturate (AudioBlock audio, int i, double w)
    {
        for (int ch = 0; ch < audio.numChannels; ++ch)
        {
            float& sample = audio.channels[ch][i];
            const double x = sample;
            const double r = channels[static_cast<std::size_t> (ch)].process (gain * x);
            sample = static_cast<float> (x + w * r / gain);
        }
    }

    std::vector<saturation::AntiAliased> channels; // per channel
    plugkit::Smoother weight, drive;
    double gain = 1.0; // Drive's, as it stands
    bool on = false;
    SaturationPosition position = SaturationPosition::After, wanted = SaturationPosition::After;
    bool arrived = false; // moved to position since its last process()
};

} // namespace comp1
