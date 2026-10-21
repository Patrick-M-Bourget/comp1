#pragma once

// The seam between the compressor and its Characters (ADR 0002): each Character's gain computer and detector, and its
// Colour mapping, chosen by Settings::character. Clean's pieces are in Clean.h, VCA's in Vca.h and FET's in Fet.h; Opto
// (#21) fills in its own here, and until then plays as Clean.

#include "Clean.h"
#include "Fet.h"
#include "Vca.h"

#include "comp1/Settings.h"

#include <plugkit/OutputLevel.h>
#include <plugkit/Units.h>

#include <cmath>

namespace comp1
{

// The Character that plays for character: Opto plays as Clean until it brings its own DSP.
inline Character played (Character character)
{
    return character == Character::Opto ? Character::Clean : character;
}

// One detection part's detector, for every Character: a sample of the detection signal in, the Gain Reduction to
// apply out. It holds the Gain Reduction it is producing, and process() takes the detection sample rather than a
// level, so FET, whose detection hears its own gain, solves its loop with it. A Character change keeps the state, so
// the incoming Character starts from the outgoing one's Gain Reduction.
class CharacterDetector
{
public:
    void prepare (double sampleRate)
    {
        detector.prepare (sampleRate);
        rms.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        detector.reset();
        rms.reset();
        levelDb = plugkit::levelFloorDb;
    }

    // At the start of each run: Auto Release moves on by the run just heard.
    void startRun (double releaseMs, bool autoReleaseOn) { detector.startRun (releaseMs, autoReleaseOn); }

    // Takes one sample of the detection signal and returns the Gain Reduction to apply, in dB. Clean and FET read the
    // sample's peak level, and VCA its RMS level; each feeds Clean's gain computer and detector. VCA's RMS level runs on
    // every Character, so a change to VCA starts from a settled level.
    //
    // On FET with an Internal Detection Source the detector is fed back: its gain computer reads the detection sample
    // times the gain applied at that same sample, the level less the Gain Reduction, so each step of the detector is
    // FET's loop solved within the sample (fet::step). The detection signal is the compressor's input as the Wet path
    // delays it for the detector, so in Zero Latency that is the output's level, and in High Quality the undelayed
    // input's times the gain, Lookahead ahead of the delayed audio (ADR 0002). With an External Detection Source the
    // gain never reaches the Sidechain, so FET is feedforward, as Clean is.
    double process (Character character, DetectionSource source, float sample, const clean::Curve& curve, const clean::Timing& timing)
    {
        const double rmsDb = rms.process (sample);
        levelDb = character == Character::VCA ? rmsDb : plugkit::gainToDecibels (std::abs (static_cast<double> (sample)));
        const double wantedDb = clean::gainReduction (levelDb, curve);
        if (character == Character::FET && source == DetectionSource::Internal)
            return detector.process (wantedDb, timing, [&] (double previousDb, double pole) { return fet::step (previousDb, levelDb, wantedDb, pole, curve); });
        return detector.process (wantedDb, timing);
    }

    // The Detection Level the last sample read, in dB where a full-scale sine reads 0: the detection signal's level,
    // before any feedback.
    double detectionLevelDb() const { return levelDb; }

private:
    clean::Detector detector;
    vca::RmsLevel rms;
    double levelDb = plugkit::levelFloorDb;
};

// Whether a Character has Colour: Clean has none, at any Colour.
inline bool hasColour (Character character) { return character == Character::VCA || character == Character::FET; }

// A Character's Colour at a Gain Reduction in dB: the residual's weight, before the Colour control scales it, and the
// drive. Clean has none: a weight of exactly 0.
inline ColourAt colourOf (Character character, double gainReductionDb)
{
    switch (character)
    {
        case Character::VCA: return colourAt (vca::colour, gainReductionDb);
        case Character::FET: return colourAt (fet::colour, gainReductionDb);
        case Character::Clean:
        case Character::Opto: break;
    }
    return {};
}

} // namespace comp1
