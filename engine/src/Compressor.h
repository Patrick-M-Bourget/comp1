#pragma once

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

#include "Character.h"
#include "Clean.h"
#include "Colour.h"
#include "Detection.h"
#include "Glide.h"
#include "LevelTaps.h"
#include "StereoPlacement.h"
#include "comp1/AutoGain.h"
#include "comp1/Engine.h"
#include "comp1/Settings.h"

#include <plugkit/OutputLevel.h>
#include <plugkit/Placement.h>
#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

namespace comp1
{

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

    // Auto Gain's estimate for the settings as they stand, in dB (comp1/AutoGain.h), with the stored Auto Gain on, and
    // 0 dB with it off, whatever Comp Follow (ADR 0004): from the mixed settings' Transfer Curve while Comp Follow's
    // share is above 0 (all of it while following), so it glides with the Comp Curve, and the stored settings' alone
    // at 0; and from the timing and the Character asked for, not the ones mid-glide, so a change of Character moves it
    // at once while the voices crossfade.
    double autoGainDb() const { return storedAutoGain ? autogain::estimateDb (mixed(), estimatedTiming) : 0.0; }

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

} // namespace comp1
