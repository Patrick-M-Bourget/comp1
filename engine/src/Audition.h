#pragma once

// Detection Audition: plays the detection signal in place of the Wet path, fading between the two over the Smoother's
// glide time, so holding it and letting go never jump. It plays the part Stereo Placement compresses, as the
// detector hears it. Under Stereo, mapping the detection channels to the output's: a stereo detection signal plays
// each channel on its own output channel, a mono one on every output channel, and a stereo one on a mono output as the
// average of its two channels. Under Mid or Side, on a stereo track, the detection signal's Mid or Side as plugkit
// places a part on its own (placedSample): the Mid on both channels, the Side on the left and, inverted, the right. A
// change of Placement fades from the old part to the new over the glide time; one asked for during the fade starts
// once it is over. In High Quality it is delayed as the Wet path is up to here (comp1/Latency.h's
// wetPathLatencySamples) less the Oversampling section ahead of the detection stage, which has delayed the detection
// signal already, so it keeps the reported latency and fades against the Wet path in time.

#include "Crossfade.h"
#include "DelayLine.h"
#include "Detection.h"
#include "Glide.h"
#include "StereoPlacement.h"
#include "comp1/Engine.h"
#include "comp1/Latency.h"
#include "comp1/Settings.h"

#include <plugkit/Placement.h>
#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace comp1
{

class Audition
{
public:
    // Allocates for up to numChannels output channels of up to a run a piece.
    void prepare (double sampleRate, int numChannels)
    {
        stereoTrack = numChannels == 2;
        auditionShare.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        auditionShare.reset (0.0);
        placementFade.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        placementFade.reset (1.0);
        placementFading = false;
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        storage.assign (channels, std::vector<float> (static_cast<std::size_t> (plugkit::runSamples)));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        delay.prepare (numChannels, wetPathLatencySamples (ProcessingMode::HighQuality, sampleRate) - oversamplingSectionSamples);
    }

    // With snap, it is held or let go at once instead of fading.
    void setHeld (bool held, bool snap)
    {
        glideTo (auditionShare, held ? 1.0 : 0.0, snap);
    }

    // With snap, the new Placement plays at once instead of fading in. A mono track plays as Stereo.
    void setPlacement (StereoPlacement newPlacement, bool snap)
    {
        asked = stereoTrack ? routed (newPlacement) : plugkit::StereoPlacement::Stereo;
        if (snap)
        {
            placement = asked;
            placementFading = false;
        }
    }

    // detection is the piece of the detection signal of the samples now coming in (in High Quality, as the Oversampling
    // section ahead of the detection stage has delayed them), audio the Wet path's piece, which High Quality has
    // delayed by wetPathLatencySamples. The detection signal, mapped to the output's channels, feeds
    // the delay in both Processing Modes, so a switch to High Quality starts from the latest of it.
    void process (const DetectionSignal& detection, AudioBlock audio, bool highQuality)
    {
        const AudioBlock heard { pointers.data(), audio.numChannels, audio.numSamples };
        for (int i = 0; i < heard.numSamples; ++i)
        {
            if (! placementFading && asked != placement)
            {
                leaving = placement;
                placement = asked;
                placementFade.reset (0.0);
                placementFade.setTarget (1.0);
                placementFading = true;
            }
            const double in = placementFading ? placementFade.next() : 1.0;
            for (int ch = 0; ch < heard.numChannels; ++ch)
            {
                const float now = detectionSample (detection, placement, audio.numChannels, ch, i);
                heard.channels[ch][i] = placementFading
                                            ? static_cast<float> (in * now + (1.0 - in) * detectionSample (detection, leaving, audio.numChannels, ch, i))
                                            : now;
            }
            if (placementFading && placementFade.isSettled())
                placementFading = false;
        }
        if (highQuality)
            delay.process (heard);
        else
            delay.write ({ heard.channels, heard.numChannels, heard.numSamples });

        // The Wet path fades toward the detection signal as heard; let go and settled, it plays bit for bit.
        crossfade (auditionShare, audio, { heard.channels, heard.numChannels, heard.numSamples });
    }

private:
    static float detectionSample (const DetectionSignal& detection, plugkit::StereoPlacement placement, int outputChannels, int ch, int i)
    {
        if (placement != plugkit::StereoPlacement::Stereo)
            return plugkit::placedSample (placement, outputChannels, ch, [&] (int) { return detection.partFollowedBy (placement)[i]; });
        if (detection.numChannels == 1)
            return detection.channels[0][i];
        if (outputChannels == 1)
            return 0.5f * (detection.channels[0][i] + detection.channels[1][i]);
        return detection.channels[ch][i];
    }

    plugkit::Smoother auditionShare; // 0 plays the Wet path, 1 the detection signal
    bool stereoTrack = false;
    plugkit::StereoPlacement asked = plugkit::StereoPlacement::Stereo;     // the Placement asked for
    plugkit::StereoPlacement placement = plugkit::StereoPlacement::Stereo; // the one playing, or fading in
    plugkit::StereoPlacement leaving = plugkit::StereoPlacement::Stereo;   // while one fades in, the one fading out
    plugkit::Smoother placementFade;                                       // the share of placement, while it fades in
    bool placementFading = false;
    std::vector<std::vector<float>> storage; // per output channel, the detection signal as heard
    std::vector<float*> pointers;            // per output channel, into storage
    DelayLine delay;                         // High Quality's, per output channel
};

} // namespace comp1
