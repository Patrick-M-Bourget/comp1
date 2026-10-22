#include "comp1/Engine.h"

#include "Character.h"
#include "Clean.h"
#include "Colour.h"
#include "Crossfade.h"
#include "DelayLine.h"
#include "Detection.h"
#include "DryWet.h"
#include "Gate.h"
#include "Glide.h"
#include "LevelTaps.h"
#include "Limiter.h"
#include "Lookahead.h"
#include "ModeFade.h"
#include "OversampledSection.h"
#include "SaturationStage.h"
#include "comp1/AutoGain.h"
#include "comp1/Latency.h"

#include <plugkit/LatestValue.h>
#include <plugkit/NoSubnormals.h>
#include <plugkit/OutputLevelTap.h>
#include <plugkit/Placement.h>
#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

namespace comp1
{

namespace
{

// comp1's Stereo Placement as plugkit routes it.
plugkit::StereoPlacement routed (StereoPlacement placement)
{
    switch (placement)
    {
        case StereoPlacement::Stereo: return plugkit::StereoPlacement::Stereo;
        case StereoPlacement::Mid: return plugkit::StereoPlacement::Mid;
        case StereoPlacement::Side: return plugkit::StereoPlacement::Side;
    }
    return plugkit::StereoPlacement::Stereo;
}

// The compressor: its Character's gain computer and detector (Character.h) on the part of the signal Stereo Placement
// picks, and its Character's Colour controls for that part, which it hands the After section (Colour.h): it applies
// only the gain.
// With Stereo, on every channel, each main channel detected from what it follows in the detection signal, with Stereo
// Link between the two channels of a stereo track; with Mid or Side, on that part alone, detected from the detection
// signal's own Mid or Side. A mono track is compressed as Stereo, whatever Placement is stored.
//
// Its settings glide with plugkit's Smoother, a step of a whole run at each run's start, so a change, Host Automation
// included, is click-free and glides the same at every host block size; the gain computer and detector read the
// settings as they stand for the run. Ratio glides as 1/Ratio, so ∞:1 (0) is reached; Attack and Release glide in
// log time, as their host parameters move. Stereo Link glides as a fraction, 0 to 1, a sample at a time: it moves the
// Gain Reduction itself, after the detectors, where a step a run would put a corner in the gain every run. Colour glides
// as a fraction, 0 to 1, a sample at a time too, as it shapes the waveform, and so does Threshold as Colour tracks it.
//
// Comp Follow (ADR 0004): the gain computer, Colour's Threshold tracking and Auto Gain's estimate take Threshold and
// 1/Ratio from the Comp Curve at Comp while it is on, with the stored Knee and Range, and the stored Threshold, Ratio
// and Auto Gain change nothing; Auto Gain's estimate reads the stored timing either way. Turning it on or off moves a
// share of the Comp Curve's values over theirs, 0 to 1, along a smoothstep over compFollowGlideMs (dyn-lab's follow_share), stepping a run at a time on the run grid: each run's start
// mixes the two by the share, (1 - share) times the stored plus share times the Comp Curve's, and the settings glide to the
// mix as any change does. Moving targets, Comp or the stored values, move the mix with them, and a switch back
// mid-glide turns the share round from where it is.
//
// A voice is a Placement and a Character, and a change of either never jumps. At the start of a run, the new voice
// starts with detectors and Colour of its own and fades in over the glide time as the old one fades out, its detectors
// still running, so the applied gain crossfades, and each voice's Colour weight carries its share; a change asked for
// during a fade starts once the fade is over. A new Placement's detectors start from no Gain Reduction, as they hear
// another part. A new Character's, on the same Placement, start from the outgoing ones' state, so from the Gain
// Reduction the outgoing Character was producing.
class Compressor
{
public:
    void prepare (double newSampleRate, int numChannels)
    {
        sampleRate = newSampleRate;
        stereoTrack = numChannels == 2;
        for (auto& voice : voices)
        {
            voice.detectors.assign (static_cast<std::size_t> (std::max (numChannels, 0)), CharacterDetector {});
            for (auto& detector : voice.detectors)
                detector.prepare (sampleRate);
            voice.levelsDb.assign (voice.detectors.size(), plugkit::levelFloorDb);
            voice.reductionsDb.assign (voice.detectors.size(), 0.0);
            voice.gains.assign (voice.detectors.size(), 1.0);
        }
        reductions.assign (voices[0].detectors.size(), 0.0);
        channelDb.assign (reductions.size(), 0.0);
        for (auto& setting : glides)
            setting.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        stereoLink.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        colourShare.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        colourThreshold.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        fade.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        fade.reset (1.0);
        fading = false;
        followStep = plugkit::runSamples / (compFollowGlideMs / 1000.0 * sampleRate);
    }

    // With snap, the settings take effect at once instead of gliding.
    void setSettings (const Settings& settings, bool snap)
    {
        stored = clean::curveOf (settings);
        storedAutoGain = settings.autoGain;
        estimatedTiming = { .attackMs = settings.attackMs,
                            .releaseMs = settings.releaseMs,
                            .holdMs = settings.holdMs,
                            .autoRelease = settings.autoRelease,
                            .character = settings.character };
        followed = stored;
        followed.thresholdDb = compThresholdDb (settings.compCurve, settings.comp);
        followed.inverseRatio = compInverseRatio (settings.compCurve, settings.comp);
        following = settings.compFollow;
        if (snap)
            followPhase = following ? 1.0 : 0.0;
        const auto target = mixed();
        for (std::size_t s = 0; s < std::size (curveGlides); ++s)
            glideTo (glides[s], target.*curveGlides[s], snap);
        glideTo (glides[logAttack], std::log (settings.attackMs), snap);
        glideTo (glides[logRelease], std::log (settings.releaseMs), snap);
        glideTo (glides[hold], settings.holdMs, snap);
        glideTo (stereoLink, settings.stereoLink / 100.0, snap);
        glideTo (colourShare, settings.colour / 100.0, snap);
        glideTo (colourThreshold, target.thresholdDb, snap);
        placement = stereoTrack ? routed (settings.stereoPlacement) : plugkit::StereoPlacement::Stereo;
        character = settings.character;
        source = settings.detectionSource;
        if (snap)
        {
            update();
            voices[playing].placement = placement;
            voices[playing].character = character;
        }
        // Auto Release switches without a glide: the switch changes only how fast the gain comes back, not where it is.
        autoRelease = settings.autoRelease;
    }

    // Auto Gain's estimate for the settings as they stand, in dB (comp1/AutoGain.h): from the mixed settings' Transfer Curve
    // while Comp Follow's share is above 0 (all of it while following), and the stored settings' alone at 0, 0 dB with
    // the stored Auto Gain off; and from the timing and the Character asked for, not the ones mid-glide, so a change of
    // Character moves it at once while the voices crossfade. Off, its share of the estimate is Comp Follow's, so it
    // glides in and out with the Comp Curve.
    double autoGainDb() const
    {
        const double share = followShare();
        const double estimate = autogain::estimateDb (mixed(), estimatedTiming);
        return storedAutoGain ? estimate : share * estimate;
    }

    // At the start of each run: Comp Follow's share moves on by a run, and the settings glide to the new mix; true when
    // it moved, so Auto Gain's estimate has too. Then the settings move on by a run, and the gain computer and detector
    // take them, and a change of voice, a Placement or a Character, starts its fade.
    bool startRun()
    {
        const bool followMoved = stepFollow();
        bool moving = false;
        for (auto& setting : glides)
            if (! setting.isSettled())
            {
                setting.skip (plugkit::runSamples);
                moving = true;
            }
        if (moving)
            update();
        if (! fading && (placement != voices[playing].placement || character != voices[playing].character))
        {
            const auto& leaving = voices[playing];
            auto& starting = voices[1 - playing];
            if (placement == leaving.placement)
                std::copy (leaving.detectors.begin(), leaving.detectors.end(), starting.detectors.begin());
            else
                for (auto& detector : starting.detectors)
                    detector.reset();
            starting.placement = placement;
            starting.character = character;
            starting.colourStarts = true;
            playing = 1 - playing;
            fade.reset (0.0);
            fade.setTarget (1.0);
            fading = true;
        }
        return followMoved;
    }

    // Applies gain to audio, sample for sample, from the detection signal of the same samples: each main channel's
    // Detection Level comes from what it follows, or the Mid or the Side, never from the audio. Each sample's
    // Detection Level, as the Placement heard reads it, and Gain Reduction, as the gain applied (after Stereo Link),
    // go to levels, taken where the detector reads them, so they follow whatever it hears. During a voice's fade the
    // gain applied is each voice's gain weighted by its share, exactly so where both compress the same part. Each
    // voice's Colour controls for the samples go to colour, for the After section.
    //
    // At each start of a run of the detection signal's own grid, which High Quality's delay moves off the audio's, each
    // detector heard's Auto Release moves on by the run just heard, so Auto Release runs on the samples the detector
    // hears in every Processing Mode.
    void process (const DetectionSignal& detection, AudioBlock audio, RunLevels& levels, ColourControls& colour)
    {
        auto& heard = voices[playing];
        auto& leaving = voices[1 - playing];
        auto& heardColour = colour.voices[playing];
        auto& leavingColour = colour.voices[1 - playing];
        startColour (heard, heardColour, true);
        startColour (leaving, leavingColour, fading);
        for (int i = 0; i < audio.numSamples; ++i)
        {
            if ((detection.runPosition + i) % plugkit::runSamples == 0)
                startDetectorRuns();
            const double link = stereoLink.isSettled() ? stereoLink.value() : stereoLink.next();
            const double share = colourShare.isSettled() ? colourShare.value() : colourShare.next();
            const double thresholdDb = colourThreshold.isSettled() ? colourThreshold.value() : colourThreshold.next();
            askGains (heard, detection, audio.numChannels, i, link);
            if (! fading)
            {
                for (std::size_t c = 0; c < partsOf (heard); ++c)
                    levels.noteDetection (heard.levelsDb[c], heard.reductionsDb[c]);
                colourControls (heard, heardColour, i, share, thresholdDb);
                // A fade over part-way through the piece: the voice gone adds nothing for the rest of it.
                if (leavingColour.active)
                    colourControls (leaving, leavingColour, i, 0.0, thresholdDb);
                applyGains (heard, audio, i);
                continue;
            }
            askGains (leaving, detection, audio.numChannels, i, link);
            const double in = fade.next();
            if (fade.isSettled())
                fading = false;
            for (int ch = 0; ch < audio.numChannels; ++ch)
            {
                const double gain = in * gainOn (heard, ch) + (1.0 - in) * gainOn (leaving, ch);
                levels.noteDetection (heard.levelsDb[partOf (heard, ch)], gain < 1.0 ? -plugkit::gainToDecibels (gain) : 0.0);
            }
            colourControls (heard, heardColour, i, in * share, thresholdDb);
            colourControls (leaving, leavingColour, i, (1.0 - in) * share, thresholdDb);
            // Each voice's change to the sample, its gain, weighted by its share of the fade.
            const float left = audio.channels[0][i];
            const float right = audio.numChannels > 1 ? audio.channels[1][i] : left;
            for (int ch = 0; ch < audio.numChannels; ++ch)
            {
                const double change = in * changeTo (heard, ch, left, right) + (1.0 - in) * changeTo (leaving, ch, left, right);
                float& sample = audio.channels[ch][i];
                if (change != 0.0)
                    sample = static_cast<float> (sample + change);
            }
        }
    }

private:
    void startDetectorRuns()
    {
        for (std::size_t v = 0; v < voices.size(); ++v)
            if (v == playing || fading)
                for (auto& detector : voices[v].detectors)
                    detector.startRun (releaseMs, autoRelease);
    }

    // One Placement, with detectors of its own (per channel under Stereo, the first for Mid or Side), and at the sample
    // the Detection Levels they read, the Gain Reductions it asks for and the gains they make (likewise).
    struct Voice
    {
        plugkit::StereoPlacement placement = plugkit::StereoPlacement::Stereo;
        Character character = Character::Clean;
        std::vector<CharacterDetector> detectors;
        std::vector<double> levelsDb, reductionsDb, gains;
        bool colourStarts = false; // started at the last run's start, so its Colour starts afresh
    };

    // How many parts voice compresses: each channel under Stereo, or the one part, Mid or Side.
    static std::size_t partsOf (const Voice& voice)
    {
        return voice.placement == plugkit::StereoPlacement::Stereo ? voice.detectors.size() : 1;
    }

    // The part of voice channel ch follows: its own under Stereo, the Mid or Side otherwise.
    static std::size_t partOf (const Voice& voice, int ch)
    {
        return voice.placement == plugkit::StereoPlacement::Stereo ? static_cast<std::size_t> (ch) : 0;
    }

    // The gain voice applies to what channel ch carries of the part it compresses.
    static double gainOn (const Voice& voice, int ch) { return voice.gains[partOf (voice, ch)]; }

    // The gain voice asks for at sample i, as a factor, exactly 1 with no Gain Reduction, and the Detection Level and
    // Gain Reduction it comes from.
    //
    // Stereo Link, after each channel's detector: each channel's Gain Reduction is its own plus Stereo Link times the
    // larger of the two less its own, worked out as (1 - Stereo Link) times its own plus Stereo Link times the larger,
    // so that 0% is exactly each channel's own and 100% exactly the larger, the same on both. A mono track has nothing
    // to link.
    void askGains (Voice& voice, const DetectionSignal& detection, int numChannels, int i, double link)
    {
        const auto gainOf = [] (double reduction) { return reduction > 0.0 ? plugkit::decibelsToGain (-reduction) : 1.0; };
        const auto& described = describe (voice.character);
        if (voice.placement != plugkit::StereoPlacement::Stereo)
        {
            auto& detector = voice.detectors[0];
            voice.reductionsDb[0] = detector.process (described, source, detection.partFollowedBy (voice.placement)[i], curve, timing);
            voice.levelsDb[0] = detector.detectionLevelDb();
            voice.gains[0] = gainOf (voice.reductionsDb[0]);
            return;
        }
        double largest = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            reductions[c] = voice.detectors[c].process (described, source, detection.channelFollowedBy (ch)[i], curve, timing);
            channelDb[c] = voice.detectors[c].detectionLevelDb();
            largest = std::max (largest, reductions[c]);
        }
        for (std::size_t c = 0; c < reductions.size(); ++c)
        {
            voice.levelsDb[c] = channelDb[c];
            voice.reductionsDb[c] = numChannels > 1 ? (1.0 - link) * reductions[c] + link * largest : reductions[c];
            voice.gains[c] = gainOf (voice.reductionsDb[c]);
        }
    }

    // At a piece's start, voice's Colour controls: its Placement, whether it plays in the piece, and whether it starts
    // there.
    static void startColour (Voice& voice, ColourVoice& colour, bool active)
    {
        colour.placement = voice.placement;
        colour.active = active;
        colour.starts = active && voice.colourStarts;
        colour.heard = false;
        if (active)
            voice.colourStarts = false;
    }

    // voice's Colour controls at sample i, for each part it compresses: from the Character's mapping of the Gain
    // Reduction applied to the part, after Stereo Link, and of Threshold as Colour tracks it, the weight scaled by share
    // (the Colour control, and any fade's share), and the drive's gain. Under Mid or Side there is the one part. A
    // Character with no Colour has a weight of exactly 0.
    static void colourControls (const Voice& voice, ColourVoice& colour, int i, double share, double thresholdDb)
    {
        const auto n = static_cast<std::size_t> (i);
        for (std::size_t p = 0; p < partsOf (voice); ++p)
        {
            auto at = colourOf (describe (voice.character), voice.reductionsDb[p], thresholdDb);
            at.weight *= share;
            colour.weights[p][n] = at.weight;
            colour.gains[p][n] = plugkit::decibelsToGain (at.driveDb);
            colour.heard = colour.heard || at.weight != 0.0;
        }
    }

    // Applies the gains voice asks for to sample i, alone. Stereo scales each channel. Mid and Side, on a stereo track,
    // go on as a change to Left and Right (changeTo), never an encode then a decode, which float doesn't round-trip: the
    // other part passes. With no Gain Reduction the sample is left as it is, bit for bit.
    static void applyGains (const Voice& voice, AudioBlock audio, int i)
    {
        if (voice.placement == plugkit::StereoPlacement::Stereo)
        {
            for (int ch = 0; ch < audio.numChannels; ++ch)
            {
                float& sample = audio.channels[ch][i];
                const double gain = voice.gains[static_cast<std::size_t> (ch)];
                if (gain != 1.0)
                    sample = static_cast<float> (gain * sample);
            }
            return;
        }
        if (voice.gains[0] == 1.0)
            return;
        const float left = audio.channels[0][i], right = audio.channels[1][i];
        for (int ch = 0; ch < 2; ++ch)
            audio.channels[ch][i] = static_cast<float> (audio.channels[ch][i] + changeTo (voice, ch, left, right));
    }

    // What the gains voice asks for add to channel ch of a sample whose Left and Right are left and right: (g - 1) times
    // what channel ch carries of the part it compresses, as plugkit places a part (placedSample). Under Stereo, the
    // channel itself; under Mid, the Mid, on Left and on Right; under Side, the Side, added to Left and taken from
    // Right.
    static double changeTo (const Voice& voice, int ch, float left, float right)
    {
        const float carried = plugkit::placedSample (voice.placement, 2, ch, [&] (int part) {
            if (voice.placement == plugkit::StereoPlacement::Stereo)
                return part == 0 ? left : right;
            return voice.placement == plugkit::StereoPlacement::Mid ? plugkit::encodeMid (left, right) : plugkit::encodeSide (left, right);
        });
        return (gainOn (voice, ch) - 1.0) * carried;
    }

    enum Glide : std::size_t { threshold, inverseRatio, knee, range, logAttack, logRelease, hold, count };
    // The gain computer's settings, each gliding as the Glide of its index.
    static constexpr double clean::Curve::* curveGlides[] = { &clean::Curve::thresholdDb, &clean::Curve::inverseRatio, &clean::Curve::kneeDb,
                                                              &clean::Curve::rangeDb };

    // Comp Follow's share of the Comp Curve's values, 0 to 1: the smoothstep of its phase, so its glide starts and ends
    // without a corner.
    double followShare() const { return followPhase * followPhase * (3.0 - 2.0 * followPhase); }

    // The gain computer's settings as Comp Follow's share mixes them: the stored ones at 0, the Comp Curve's at 1, exactly.
    clean::Curve mixed() const
    {
        if (followPhase <= 0.0)
            return stored;
        if (followPhase >= 1.0)
            return followed;
        const double share = followShare();
        auto between = stored;
        between.thresholdDb = (1.0 - share) * stored.thresholdDb + share * followed.thresholdDb;
        between.inverseRatio = (1.0 - share) * stored.inverseRatio + share * followed.inverseRatio;
        return between;
    }

    // Moves Comp Follow's phase a run toward on or off, and the settings it mixes with it; false when it is there.
    bool stepFollow()
    {
        const double goal = following ? 1.0 : 0.0;
        if (followPhase == goal)
            return false;
        followPhase = following ? std::min (followPhase + followStep, 1.0) : std::max (followPhase - followStep, 0.0);
        const auto target = mixed();
        glides[threshold].setTarget (target.thresholdDb);
        glides[inverseRatio].setTarget (target.inverseRatio);
        colourThreshold.setTarget (target.thresholdDb);
        return true;
    }

    void update()
    {
        for (std::size_t s = 0; s < std::size (curveGlides); ++s)
            curve.*curveGlides[s] = glides[s].value();
        releaseMs = std::exp (glides[logRelease].value());
        timing = clean::timing (std::exp (glides[logAttack].value()), releaseMs, glides[hold].value(), sampleRate);
    }

    double sampleRate = 44100.0;
    plugkit::Smoother glides[count];
    plugkit::Smoother stereoLink;  // 0 to 1
    plugkit::Smoother colourShare; // Colour, 0 to 1
    // Threshold as Colour tracks it, in dB, gliding a sample at a time: it drives the shape, where the gain computer's
    // step a run would put a corner in the waveform every run.
    plugkit::Smoother colourThreshold;
    clean::Curve curve;
    clean::Timing timing;
    // Comp Follow: the gain computer's settings as stored and as the Comp Curve sets them at Comp (the stored Knee and
    // Range), whether it is on, its phase, 0 to 1, and how far a run moves it.
    clean::Curve stored, followed;
    bool storedAutoGain = false;
    autogain::Timing estimatedTiming; // the timing and Character Auto Gain's estimate reads: the stored ones
    bool following = false;
    double followPhase = 0.0;
    double followStep = 1.0;
    double releaseMs = 100.0;
    bool autoRelease = false;
    bool stereoTrack = false;
    plugkit::StereoPlacement placement = plugkit::StereoPlacement::Stereo; // the Placement asked for
    Character character = Character::Clean;                                // the Character asked for
    // The Detection Source asked for, which each detector reads: a fed-back Character (FET, Opto) detects from its own
    // gain on Internal, and is feedforward from the Sidechain on External (Character.h's CharacterDetector::process).
    DetectionSource source = DetectionSource::Internal;
    std::array<Voice, 2> voices;
    std::size_t playing = 0; // the voice heard, or fading in
    plugkit::Smoother fade;  // the share of the voice playing, while the other fades out
    bool fading = false;
    std::vector<double> reductions; // per channel, each detector's Gain Reduction at the sample
    std::vector<double> channelDb;  // per channel, the Detection Level at the sample
};

// A gain applied to every channel, in dB: Auto Gain, and Output Gain. It glides to each new target with plugkit's
// Smoother, so a change, Host Automation included, is click-free.
class GlidingGain
{
public:
    void prepare (double sampleRate)
    {
        gain.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        gain.reset (1.0);
    }

    // With snap, the new gain takes effect at once instead of gliding.
    void setTargetDb (double db, bool snap)
    {
        glideTo (gain, plugkit::decibelsToGain (db), snap);
    }

    void process (AudioBlock block)
    {
        // Settled on exactly 1 at 0 dB, so the output is the input, bit for bit, with no work.
        if (gain.isSettled() && gain.value() == 1.0)
            return;
        // The gain glides a sample at a time; once it has settled, next() would return its goal exactly, so the rest
        // is scaled by that as a constant, sample for sample the same.
        int i = 0;
        for (; i < block.numSamples && ! gain.isSettled(); ++i)
        {
            const double g = gain.next();
            for (int ch = 0; ch < block.numChannels; ++ch)
                block.channels[ch][i] = static_cast<float> (g * block.channels[ch][i]);
        }
        const double g = gain.value();
        for (int ch = 0; ch < block.numChannels; ++ch)
            for (int n = i; n < block.numSamples; ++n)
                block.channels[ch][n] = static_cast<float> (g * block.channels[ch][n]);
    }

private:
    plugkit::Smoother gain;
};

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

// Global Bypass: crossfades the output to the reference, the input after the non-finite scrub, delayed by the latency
// of the Processing Mode playing (comp1/Latency.h), so the processed output and the reference have the same delay and
// the crossfade never comb-filters. The reference's delay is fed in both Processing Modes, so a switch to High
// Quality starts from real recent audio.
class GlobalBypass
{
public:
    // Allocates for up to numChannels channels of up to a run a piece.
    void prepare (double sampleRate, int numChannels)
    {
        share.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        share.reset (0.0);
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        storage.assign (channels, std::vector<float> (static_cast<std::size_t> (plugkit::runSamples)));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = storage[ch].data();
        delay.prepare (numChannels, latencySamples (ProcessingMode::HighQuality, sampleRate));
    }

    // With snap, it is on or off at once instead of crossfading.
    void setOn (bool on, bool snap) { glideTo (share, on ? 1.0 : 0.0, snap); }

    // Takes the piece of input, before anything processes it, as the reference for the same samples' output, delayed
    // on path.
    void takeInput (ConstAudioBlock input, ProcessingMode path)
    {
        const AudioBlock taken { pointers.data(), input.numChannels, input.numSamples };
        for (int ch = 0; ch < input.numChannels; ++ch)
            std::copy (input.channels[ch], input.channels[ch] + input.numSamples, taken.channels[ch]);
        delay.process ({ taken.channels, taken.numChannels, taken.numSamples }, taken, path == ProcessingMode::HighQuality ? delay.getLength() : 0);
    }

    // Crossfades the processed piece of the same samples toward the reference.
    void process (AudioBlock processed)
    {
        crossfade (share, processed, { pointers.data(), processed.numChannels, processed.numSamples });
    }

private:
    plugkit::Smoother share;                 // 0 plays the processed output, 1 the reference
    std::vector<std::vector<float>> storage; // per channel, the reference for the piece
    std::vector<float*> pointers;            // per channel, into storage
    DelayLine delay;                         // High Quality's, per channel
};

} // namespace

struct Engine::Impl
{
    int numChannels = 0;
    double sampleRate = 44100.0;

    plugkit::LatestValue<Settings> handoff;
    Settings settings;          // the newest settings the audio thread has taken
    bool snapToSettings = true; // after prepare, settings apply at once instead of gliding

    // Where the next sample falls in the grid of runs of plugkit::runSamples samples that starts at prepare() and
    // carries across process() calls.
    int gridPosition = 0;
    std::vector<float*> subBlock; // per channel

    GateStage gate;
    DetectionStage detection;
    Compressor compressor;
    ColourControls colourControls; // the compressor's, for the After section
    ColourStage colour;
    SaturationStage saturation;
    GlidingGain autoGain, outputGain;
    // High Quality's delays (comp1/Latency.h): the Oversampling sections, Before the detection stage (with the
    // Sidechain's delay, so External detection meets the audio as Internal does) and After the compressor, and the
    // Lookahead budget around the compressor; the Limiter fills the Limiter reserve.
    OversampledSection beforeSection, afterSection;
    // Zero Latency's After section at the host rate: its input, then its residual, per channel.
    std::vector<std::vector<double>> hostRate;
    std::vector<double*> hostRatePointers;
    DelayLine sidechainDelay;
    std::array<std::vector<float>, 2> sidechainStorage; // the Sidechain's piece, delayed, or silence
    std::array<float*, 2> sidechainPointers {};
    std::array<const float*, 2> sidechainConstPointers {};
    ConstAudioBlock heardSidechain;
    std::vector<float> silence; // a run of it, for the Sidechain's delay when nothing is connected
    LookaheadStage lookahead;
    Limiter limiter;
    ModeFade modeFade;
    Audition audition;
    DryWet dryWet;
    GlobalBypass globalBypass;

    // The Output Level, per channel. Fixed in size, so a reader never meets a tap a prepare() has moved.
    static constexpr int maxOutputLevelChannels = 2;
    std::array<plugkit::OutputLevelTap, maxOutputLevelChannels> outputLevels;
    std::atomic<int> outputLevelChannels { 0 };
    RunLevels runLevels;
    LevelHistory levelHistory;
    DetectionSourceTap detectionSourceTap;

    void applySettings()
    {
        const auto* latest = handoff.takeLatest();
        if (latest == nullptr && ! snapToSettings)
            return;
        if (latest != nullptr)
            settings = *latest;
        gate.setSettings (settings, snapToSettings);
        detection.setSource (settings.detectionSource);
        detection.setDetectionEq (settings.detectionEq, snapToSettings);
        compressor.setSettings (settings, snapToSettings);
        autoGain.setTargetDb (compressor.autoGainDb(), snapToSettings);
        saturation.setSettings (settings, snapToSettings);
        dryWet.setLevels (settings.dryDb, settings.wetDb, snapToSettings);
        outputGain.setTargetDb (settings.outputGainDb, snapToSettings);
        limiter.setOn (settings.limiter, snapToSettings);
        limiter.setCeiling (settings.ceilingDb, snapToSettings);
        audition.setHeld (settings.detectionAudition, snapToSettings);
        audition.setPlacement (settings.stereoPlacement, snapToSettings);
        lookahead.setLookahead (settings.lookaheadMs, snapToSettings);
        const auto oversampling = oversamplingInForce (settings.oversampling, sampleRate);
        beforeSection.setOversampling (oversampling, snapToSettings);
        afterSection.setOversampling (oversampling, snapToSettings);
        glideTo (modeFade, settings.processingMode, snapToSettings);
        globalBypass.setOn (settings.globalBypass, snapToSettings);
        snapToSettings = false;
    }

    // The piece of main's first channels from start, for count samples, in place: a non-finite input sample (nan,
    // inf) never reaches the output, nor the detector, whose Gain Reduction it would hold forever, so it plays as
    // silence. Its Input Level is noted.
    AudioBlock sanitisedInput (AudioBlock main, int channels, int start, int count)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            float* samples = main.channels[ch] + start;
            subBlock[static_cast<std::size_t> (ch)] = samples;
            for (int i = 0; i < count; ++i)
                samples[i] = std::isfinite (samples[i]) ? samples[i] : 0.0f;
            runLevels.noteInput (samples, count);
        }
        return { subBlock.data(), channels, count };
    }

    // The Wet path, on a piece of a run, in place, gated, with the Sidechain's piece (or none), on path: Saturation
    // Before, the detection stage, fed the compressor's input as the Internal signal, then the compressor, Colour and
    // Saturation After, and Auto Gain; then the Dry/Wet sum, Detection Audition and Output Gain. Saturation runs at one
    // of its two positions. The compressor's settings step at the start of each run, wherever the host's blocks fall;
    // Saturation, Auto Gain, Detection Audition and Output Gain glide a sample at a time with next(), so where the
    // pieces fall changes nothing either.
    // From the sample a new setting is taken (the start of the host's block, so the host decides when a change lands),
    // the output is the same sample for sample however the audio is cut. The detector runs while Detection Audition
    // is held, so letting go doesn't restart compression from zero.
    //
    // In High Quality, Saturation runs in its Oversampling section, Before or After (ADR 0009), each delaying the audio
    // by a section whether Saturation is in it or not, the Sidechain delayed with the Before one. Colour runs in the
    // After section too, ahead of Saturation After. The audio is delayed by the Lookahead budget and the detection
    // signal by less, so the compressor meets the audio Lookahead early; the two sections and the budget are the Wet
    // path's delay up to the Dry/Wet sum. After Output Gain, the Limiter, on the path playing. Then Global Bypass, which crossfades
    // to the input as it came in, before the Gate, delayed to match; it takes precedence over Detection Audition, and
    // everything before it runs on while it is on, so turning it off has no restart transient. Last, the mode fade,
    // which swaps the paths in silence.
    void wetPath (AudioBlock wet, const ConstAudioBlock* sidechainPiece, ProcessingMode path)
    {
        if (gridPosition == 0)
        {
            saturation.startRun();
            beforeSection.startRun();
            afterSection.startRun();
        }
        const bool highQuality = path == ProcessingMode::HighQuality;
        oversampledSaturation (beforeSection, SaturationPosition::Before, wet, highQuality);
        const auto* sidechainHeard = delayedSidechain (sidechainPiece, wet.numSamples, highQuality);
        const auto detected = detection.process ({ wet.channels, wet.numChannels, wet.numSamples }, sidechainHeard);
        detectionSourceTap.push (detected.sourceMean, detected.numSamples);
        if (gridPosition == 0)
        {
            // Comp Follow's glide moves Auto Gain's estimate a run at a time.
            if (compressor.startRun())
                autoGain.setTargetDb (compressor.autoGainDb(), false);
            lookahead.startRun();
        }
        compressor.process (lookahead.process (detected, wet, highQuality), wet, runLevels, colourControls);
        after (wet, highQuality);
        autoGain.process (wet);
        dryWet.sum (wet);
        audition.process (detected, wet, highQuality);
        outputGain.process (wet);
        limiter.process (wet, path, runLevels);
        globalBypass.process (wet);
        modeFade.process (wet);
    }

    // Saturation at its position on wet: in High Quality, in that position's Oversampling section, which delays wet by
    // a section whether Saturation is there or not; in Zero Latency at the host rate, with the section fed so it holds
    // the latest audio.
    void oversampledSaturation (OversampledSection& section, SaturationPosition at, AudioBlock wet, bool highQuality)
    {
        if (! highQuality)
        {
            section.write ({ wet.channels, wet.numChannels, wet.numSamples });
            saturation.process (at, wet);
            return;
        }
        section.process (wet, saturation.heardAt (at), [&] (ConstAudioBlock host, double* const* oversampled, int factor) {
            return saturation.residual (at, host, oversampled, factor);
        });
    }

    // The After section on wet, the compressed signal: Colour, then Saturation After, which hears the coloured signal,
    // their residuals summed. In High Quality, in the After section at the Oversampling in force; in Zero Latency the
    // same residual at the host rate, added in the same way, with the section fed so it holds the latest audio. With
    // neither adding anything, the section is a plain delay in High Quality, and Zero Latency leaves wet as it is.
    void after (AudioBlock wet, bool highQuality)
    {
        colour.startPiece (colourControls);
        const bool colourHeard = colourControls.heard();
        const bool heard = colourHeard || saturation.heardAt (SaturationPosition::After);
        const auto residual = [this, colourHeard] (ConstAudioBlock host, double* const* oversampled, int factor) {
            return afterResidual (host, oversampled, factor, colourHeard);
        };
        if (highQuality)
        {
            afterSection.process (wet, heard, residual);
            return;
        }
        const ConstAudioBlock host { wet.channels, wet.numChannels, wet.numSamples };
        afterSection.write (host);
        if (heard)
            for (int ch = 0; ch < wet.numChannels; ++ch)
                std::copy_n (wet.channels[ch], wet.numSamples, hostRatePointers[static_cast<std::size_t> (ch)]);
        if (! residual (host, hostRatePointers.data(), 1))
            return;
        // As High Quality's section adds the residual at 1x.
        for (int ch = 0; ch < wet.numChannels; ++ch)
            for (int i = 0; i < wet.numSamples; ++i)
                wet.channels[ch][i] = static_cast<float> (wet.channels[ch][i] + hostRatePointers[static_cast<std::size_t> (ch)][i]);
    }

    // The After section's residual (OversampledSection::process's), replacing oversampled, host's samples at factor
    // samples each: Colour's, worked out on oversampled, plus Saturation After's on oversampled with Colour's added,
    // so it hears the coloured signal. False, oversampled left alone, when neither adds anything; each then follows
    // host.
    bool afterResidual (ConstAudioBlock host, double* const* oversampled, int factor, bool colourHeard)
    {
        constexpr auto at = SaturationPosition::After;
        if (! colourHeard)
        {
            colour.follow (colourControls, host.channels, host.numChannels, host.numSamples);
            return saturation.residual (at, host, oversampled, factor);
        }
        colour.residual (colourControls, oversampled, host.numChannels, host.numSamples, factor);
        const double* const* coloured = colour.residuals();
        const int count = host.numSamples * factor;
        const auto addColour = [&] {
            for (int ch = 0; ch < host.numChannels; ++ch)
                for (int k = 0; k < count; ++k)
                    oversampled[ch][k] += coloured[ch][k];
        };
        if (saturation.heardAt (at))
            addColour();
        if (saturation.residual (at, host, oversampled, factor))
            addColour();
        else
            for (int ch = 0; ch < host.numChannels; ++ch)
                std::copy_n (coloured[ch], count, oversampled[ch]);
        return true;
    }

    // The Sidechain's piece as the detection stage hears it: in High Quality delayed by the Before section's delay,
    // as the audio is, so External detection keeps Internal's timing. Its delay is fed in both Processing Modes, with
    // silence when nothing is connected, so a switch or a connection starts from the latest of it.
    const ConstAudioBlock* delayedSidechain (const ConstAudioBlock* sidechainPiece, int numSamples, bool highQuality)
    {
        std::array<const float*, 2> in {};
        for (std::size_t ch = 0; ch < in.size(); ++ch)
            in[ch] = sidechainPiece == nullptr ? silence.data()
                                               : sidechainPiece->channels[std::min (static_cast<int> (ch), sidechainPiece->numChannels - 1)];
        if (! highQuality)
        {
            sidechainDelay.write ({ in.data(), 2, numSamples });
            return sidechainPiece;
        }
        sidechainDelay.process ({ in.data(), 2, numSamples }, { sidechainPointers.data(), 2, numSamples }, sidechainDelay.getLength());
        if (sidechainPiece == nullptr)
            return nullptr;
        heardSidechain = { sidechainConstPointers.data(), sidechainPiece->numChannels, numSamples };
        return &heardSidechain;
    }

    // The Output Level of what the piece sends on, after everything.
    void noteOutputLevels (AudioBlock out)
    {
        for (int ch = 0; ch < std::min (out.numChannels, maxOutputLevelChannels); ++ch)
            outputLevels[static_cast<std::size_t> (ch)].process (out.channels[ch], out.numSamples);
        for (int ch = 0; ch < out.numChannels; ++ch)
            runLevels.noteOutput (out.channels[ch], out.numSamples);
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int, int numChannels)
{
    impl->numChannels = numChannels;
    impl->gridPosition = 0;
    impl->subBlock.assign (static_cast<std::size_t> (std::max (numChannels, 0)), nullptr);
    impl->gate.prepare (sampleRate, numChannels);
    impl->detection.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->compressor.prepare (sampleRate, numChannels);
    impl->colourControls.prepare (numChannels, plugkit::runSamples);
    impl->colour.prepare (numChannels, plugkit::runSamples * factorOf (maxOversampling (sampleRate)));
    impl->hostRate.assign (static_cast<std::size_t> (std::max (numChannels, 0)), std::vector<double> (static_cast<std::size_t> (plugkit::runSamples)));
    impl->hostRatePointers.clear();
    for (auto& channel : impl->hostRate)
        impl->hostRatePointers.push_back (channel.data());
    impl->saturation.prepare (sampleRate, numChannels);
    impl->autoGain.prepare (sampleRate);
    impl->outputGain.prepare (sampleRate);
    impl->lookahead.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->sampleRate = sampleRate;
    impl->beforeSection.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->afterSection.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->sidechainDelay.prepare (2, oversamplingSectionSamples);
    impl->silence.assign (static_cast<std::size_t> (plugkit::runSamples), 0.0f);
    for (std::size_t ch = 0; ch < impl->sidechainStorage.size(); ++ch)
    {
        impl->sidechainStorage[ch].assign (static_cast<std::size_t> (plugkit::runSamples), 0.0f);
        impl->sidechainPointers[ch] = impl->sidechainStorage[ch].data();
        impl->sidechainConstPointers[ch] = impl->sidechainStorage[ch].data();
    }
    impl->limiter.prepare (sampleRate, numChannels);
    impl->modeFade.prepare (sampleRate);
    impl->audition.prepare (sampleRate, numChannels);
    impl->dryWet.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->globalBypass.prepare (sampleRate, numChannels);
    const int outputLevelChannels = std::clamp (numChannels, 0, Impl::maxOutputLevelChannels);
    for (int ch = 0; ch < outputLevelChannels; ++ch)
        impl->outputLevels[static_cast<std::size_t> (ch)].prepare (sampleRate);
    impl->outputLevelChannels.store (outputLevelChannels, std::memory_order_release);
    impl->runLevels = {};
    impl->levelHistory.prepare (sampleRate);
    impl->detectionSourceTap.prepare (sampleRate);
    impl->snapToSettings = true;
}

void Engine::setSettings (const Settings& settings)
{
    impl->handoff.publish (settings);
}

void Engine::process (AudioBlock main, const ConstAudioBlock* sidechain)
{
    const plugkit::NoSubnormals noSubnormals;
    impl->applySettings();

    const int channels = std::min (main.numChannels, impl->numChannels);
    // The Sidechain counts as connected only with a channel and a whole block of samples, and it contributes its first
    // two channels.
    const bool sidechainConnected = sidechain != nullptr && sidechain->numChannels > 0 && sidechain->numSamples >= main.numSamples;
    std::array<const float*, 2> sidechainPiece {};
    const int sidechainChannels = sidechainConnected ? std::min (sidechain->numChannels, 2) : 0;
    for (int start = 0, count = 0; start < main.numSamples; start += count)
    {
        // The rest of the grid's run, or of the block where it ends first, leaving the run open; or less, so that a
        // Processing Mode switch swaps paths between pieces.
        const auto path = impl->modeFade.startPiece();
        count = std::min ({ plugkit::runSamples - impl->gridPosition, main.numSamples - start, impl->modeFade.pieceLimit() });
        const auto wet = impl->sanitisedInput (main, channels, start, count);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            sidechainPiece[static_cast<std::size_t> (ch)] = sidechain->channels[ch] + start;
        const ConstAudioBlock sidechainBlock { sidechainPiece.data(), sidechainChannels, count };
        const auto* sidechainPieceOrNone = sidechainConnected ? &sidechainBlock : nullptr;
        // Global Bypass's reference is the input scrubbed of non-finite samples, before anything processes it.
        impl->globalBypass.takeInput ({ wet.channels, wet.numChannels, wet.numSamples }, path);
        // The Gate, ahead of the split, so it lowers the Dry and the Wet path alike.
        if (impl->gridPosition == 0)
            impl->gate.startRun();
        impl->gate.process (wet, sidechainPieceOrNone, impl->runLevels);
        // The split: the gated input is copied as the Dry path before the Wet path touches it.
        impl->dryWet.split ({ wet.channels, wet.numChannels, wet.numSamples }, path);
        impl->wetPath (wet, sidechainPieceOrNone, path);
        impl->noteOutputLevels (wet);

        impl->gridPosition += count;
        if (impl->gridPosition == plugkit::runSamples)
        {
            impl->gridPosition = 0;
            impl->levelHistory.push (impl->runLevels.take());
        }
    }
}

int Engine::outputLevelChannels() const
{
    return impl->outputLevelChannels.load (std::memory_order_acquire);
}

plugkit::OutputLevel Engine::readOutputLevel (int channel)
{
    return channel >= 0 && channel < outputLevelChannels() ? impl->outputLevels[static_cast<std::size_t> (channel)].read() : plugkit::OutputLevel {};
}

int Engine::readLevels (LevelRecord* destination, int maxRecords)
{
    return impl->levelHistory.pop (destination, maxRecords);
}

void Engine::skipLevels()
{
    impl->levelHistory.skip();
}

int Engine::levelHistoryCapacity() const
{
    return impl->levelHistory.capacity();
}

int Engine::readDetectionSourceSamples (float* destination, int maxSamples)
{
    return impl->detectionSourceTap.pop (destination, maxSamples);
}

void Engine::skipDetectionSourceSamples()
{
    impl->detectionSourceTap.skip();
}

int Engine::detectionSourceSampleCapacity() const
{
    return impl->detectionSourceTap.capacity();
}

} // namespace comp1
