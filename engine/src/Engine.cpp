#include "comp1/Engine.h"

#include "Clean.h"
#include "Crossfade.h"
#include "DelayLine.h"
#include "Detection.h"
#include "DryWet.h"
#include "Glide.h"
#include "LevelTaps.h"
#include "Limiter.h"
#include "Lookahead.h"
#include "ModeFade.h"
#include "OversampledSection.h"
#include "SaturationStage.h"
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

// The Clean Character's gain computer settings, from Settings.
clean::Curve curveOf (const Settings& settings)
{
    return { .thresholdDb = settings.thresholdDb,
             .inverseRatio = 1.0 / settings.ratio,
             .kneeDb = settings.kneeDb,
             .rangeDb = settings.rangeDb };
}

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

// The compressor: the Clean Character's gain computer and detector on the part of the signal Stereo Placement picks.
// With Stereo, on every channel, each main channel detected from what it follows in the detection signal, with Stereo
// Link between the two channels of a stereo track; with Mid or Side, on that part alone, detected from the detection
// signal's own Mid or Side. A mono track is compressed as Stereo, whatever Placement is stored.
//
// Its settings glide with plugkit's Smoother, a step of a whole run at each run's start, so a change, Host Automation
// included, is click-free and glides the same at every host block size; the gain computer and detector read the
// settings as they stand for the run. Ratio glides as 1/Ratio, so ∞:1 (0) is reached; Attack and Release glide in
// log time, as their host parameters move. Stereo Link glides as a fraction, 0 to 1, a sample at a time: it moves the
// Gain Reduction itself, after the detectors, where a step a run would put a corner in the gain every run.
//
// A change of Placement never jumps. At the start of a run, the new Placement starts with detectors of its own and
// fades in over the glide time as the old one fades out, its detectors still running; a change asked for during a fade
// starts once the fade is over.
class Compressor
{
public:
    void prepare (double newSampleRate, int numChannels)
    {
        sampleRate = newSampleRate;
        stereoTrack = numChannels == 2;
        for (auto& voice : voices)
        {
            voice.detectors.assign (static_cast<std::size_t> (std::max (numChannels, 0)), {});
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
        fade.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        fade.reset (1.0);
        fading = false;
    }

    // With snap, the settings take effect at once instead of gliding.
    void setSettings (const Settings& settings, bool snap)
    {
        const auto target = curveOf (settings);
        for (std::size_t s = 0; s < std::size (curveGlides); ++s)
            glideTo (glides[s], target.*curveGlides[s], snap);
        glideTo (glides[logAttack], std::log (settings.attackMs), snap);
        glideTo (glides[logRelease], std::log (settings.releaseMs), snap);
        glideTo (glides[hold], settings.holdMs, snap);
        glideTo (stereoLink, settings.stereoLink / 100.0, snap);
        placement = stereoTrack ? routed (settings.stereoPlacement) : plugkit::StereoPlacement::Stereo;
        if (snap)
        {
            update();
            voices[playing].placement = placement;
        }
        // Auto Release switches without a glide: the switch changes only how fast the gain comes back, not where it is.
        autoRelease = settings.autoRelease;
    }

    // At the start of each run: the settings move on by a run, and the gain computer and detector take them. Then a
    // change of Placement starts its fade.
    void startRun()
    {
        bool moving = false;
        for (auto& setting : glides)
            if (! setting.isSettled())
            {
                setting.skip (plugkit::runSamples);
                moving = true;
            }
        if (moving)
            update();
        if (! fading && placement != voices[playing].placement)
        {
            playing = 1 - playing;
            voices[playing].placement = placement;
            for (auto& detector : voices[playing].detectors)
                detector.reset();
            fade.reset (0.0);
            fade.setTarget (1.0);
            fading = true;
        }
    }

    // Applies gain to audio, sample for sample, from the detection signal of the same samples: each main channel's
    // Detection Level comes from what it follows, or the Mid or the Side, never from the audio. Each sample's
    // Detection Level, as the Placement heard reads it, and Gain Reduction, as the gain applied (after Stereo Link),
    // go to levels, taken where the detector reads them, so they follow whatever it hears. During a Placement's fade
    // the gain applied is each Placement's gain weighted by its share, exactly so where both compress the same part.
    //
    // At each start of a run of the detection signal's own grid, which High Quality's delay moves off the audio's, each
    // detector heard's Auto Release moves on by the run just heard, so Auto Release runs on the samples the detector
    // hears in every Processing Mode.
    void process (const DetectionSignal& detection, AudioBlock audio, RunLevels& levels)
    {
        auto& heard = voices[playing];
        for (int i = 0; i < audio.numSamples; ++i)
        {
            if ((detection.runPosition + i) % plugkit::runSamples == 0)
                startDetectorRuns();
            const double link = stereoLink.isSettled() ? stereoLink.value() : stereoLink.next();
            askGains (heard, detection, audio.numChannels, i, link);
            if (! fading)
            {
                for (std::size_t c = 0; c < partsOf (heard); ++c)
                    levels.noteDetection (heard.levelsDb[c], heard.reductionsDb[c]);
                applyGains (heard, audio, i);
                continue;
            }
            auto& leaving = voices[1 - playing];
            askGains (leaving, detection, audio.numChannels, i, link);
            const double in = fade.next();
            if (fade.isSettled())
                fading = false;
            for (int ch = 0; ch < audio.numChannels; ++ch)
            {
                const double gain = in * gainOn (heard, ch) + (1.0 - in) * gainOn (leaving, ch);
                levels.noteDetection (heard.levelsDb[partOf (heard, ch)], gain < 1.0 ? -plugkit::gainToDecibels (gain) : 0.0);
            }
            // Each Placement's change to the sample, weighted by its share of the fade.
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
        std::vector<clean::Detector> detectors;
        std::vector<double> levelsDb, reductionsDb, gains;
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
        // The Detection Level: the detection signal's peak level, in dB where a full-scale sine reads 0.
        const auto levelDb = [] (float sample) { return plugkit::gainToDecibels (std::abs (static_cast<double> (sample))); };
        if (voice.placement != plugkit::StereoPlacement::Stereo)
        {
            voice.levelsDb[0] = levelDb (detection.partFollowedBy (voice.placement)[i]);
            voice.reductionsDb[0] = voice.detectors[0].process (clean::gainReduction (voice.levelsDb[0], curve), timing);
            voice.gains[0] = gainOf (voice.reductionsDb[0]);
            return;
        }
        double largest = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t> (ch);
            channelDb[c] = levelDb (detection.channelFollowedBy (ch)[i]);
            reductions[c] = voice.detectors[c].process (clean::gainReduction (channelDb[c], curve), timing);
            largest = std::max (largest, reductions[c]);
        }
        for (std::size_t c = 0; c < reductions.size(); ++c)
        {
            voice.levelsDb[c] = channelDb[c];
            voice.reductionsDb[c] = numChannels > 1 ? (1.0 - link) * reductions[c] + link * largest : reductions[c];
            voice.gains[c] = gainOf (voice.reductionsDb[c]);
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
                if (const double gain = voice.gains[static_cast<std::size_t> (ch)]; gain != 1.0)
                    audio.channels[ch][i] = static_cast<float> (gain * audio.channels[ch][i]);
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
    // channel itself; under Mid, the Mid, on Left and on Right; under Side, the Side, added to Left and taken from Right.
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
    // Close enough to count as arrived, in each glide's units (dB, 1/Ratio, log ms, ms, a fraction): far below what
    // can be heard.
    static constexpr double arrivalTolerance = 1.0e-7;

    void update()
    {
        for (std::size_t s = 0; s < std::size (curveGlides); ++s)
            curve.*curveGlides[s] = glides[s].value();
        releaseMs = std::exp (glides[logRelease].value());
        timing = clean::timing (std::exp (glides[logAttack].value()), releaseMs, glides[hold].value(), sampleRate);
    }

    double sampleRate = 44100.0;
    plugkit::Smoother glides[count];
    plugkit::Smoother stereoLink; // 0 to 1
    clean::Curve curve;
    clean::Timing timing;
    double releaseMs = 100.0;
    bool autoRelease = false;
    bool stereoTrack = false;
    plugkit::StereoPlacement placement = plugkit::StereoPlacement::Stereo; // the Placement asked for
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

    DetectionStage detection;
    Compressor compressor;
    SaturationStage saturation;
    GlidingGain autoGain, outputGain;
    // High Quality's delays (comp1/Latency.h): the Oversampling sections, Before the detection stage (with the
    // Sidechain's delay, so External detection meets the audio as Internal does) and After the compressor, and the
    // Lookahead budget around the compressor; the Limiter fills the Limiter reserve.
    OversampledSection beforeSection, afterSection;
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

    void applySettings()
    {
        const auto* latest = handoff.takeLatest();
        if (latest == nullptr && ! snapToSettings)
            return;
        if (latest != nullptr)
            settings = *latest;
        detection.setSource (settings.detectionSource);
        detection.setDetectionEq (settings.detectionEq, snapToSettings);
        compressor.setSettings (settings, snapToSettings);
        autoGain.setTargetDb (settings.autoGain ? clean::autoGainDb (curveOf (settings)) : 0.0, snapToSettings);
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

    // The Wet path, on a piece of a run, in place, with the Sidechain's piece (or none), on path: Saturation Before,
    // the detection stage, fed the compressor's input as the Internal signal, then the compressor, Saturation After
    // and Auto Gain; then the Dry/Wet sum, Detection Audition and Output Gain. Saturation runs at one of its two
    // positions. The compressor's
    // settings step at the start of each run, wherever the host's blocks fall; Saturation, Auto Gain, Detection
    // Audition and Output Gain glide a sample at a time with next(), so where the pieces fall changes nothing either.
    // From the sample a new setting is taken (the start of the host's block, so the host decides when a change lands),
    // the output is the same sample for sample however the audio is cut. The detector runs while Detection Audition
    // is held, so letting go doesn't restart compression from zero.
    //
    // In High Quality, Saturation runs in its Oversampling section, Before or After (ADR 0009), each delaying the audio
    // by a section whether Saturation is in it or not, the Sidechain delayed with the Before one. Colour (#19) goes in
    // the After section, ahead of Saturation. The audio is delayed by the Lookahead budget and the detection signal by
    // less, so the compressor meets the audio Lookahead early; the two sections and the budget are the Wet path's delay
    // up to the Dry/Wet sum. After Output Gain, the Limiter, on the path playing. Then Global Bypass, which crossfades
    // to the input as it came in, delayed to match; it takes precedence over Detection Audition, and everything before
    // it runs on while it is on, so turning it off has no restart transient. Last, the mode fade, which swaps the paths
    // in silence.
    void wetPath (AudioBlock wet, const ConstAudioBlock* sidechainPiece, ProcessingMode path)
    {
        globalBypass.takeInput ({ wet.channels, wet.numChannels, wet.numSamples }, path);
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
        if (gridPosition == 0)
        {
            compressor.startRun();
            lookahead.startRun();
        }
        compressor.process (lookahead.process (detected, wet, highQuality), wet, runLevels);
        oversampledSaturation (afterSection, SaturationPosition::After, wet, highQuality);
        autoGain.process (wet);
        dryWet.sum (wet);
        audition.process (detected, wet, highQuality);
        outputGain.process (wet);
        limiter.process (wet, path);
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
    impl->detection.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->compressor.prepare (sampleRate, numChannels);
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
        // The split: the input, scrubbed of non-finite samples, is copied as the Dry path before the Wet path touches
        // it. The Gate (#17) will sit ahead of it, so it gates both paths.
        impl->dryWet.split ({ wet.channels, wet.numChannels, wet.numSamples }, path);
        for (int ch = 0; ch < sidechainChannels; ++ch)
            sidechainPiece[static_cast<std::size_t> (ch)] = sidechain->channels[ch] + start;
        const ConstAudioBlock sidechainBlock { sidechainPiece.data(), sidechainChannels, count };
        impl->wetPath (wet, sidechainConnected ? &sidechainBlock : nullptr, path);
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

} // namespace comp1
