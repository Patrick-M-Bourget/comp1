#pragma once

// The seam between the compressor and its Characters (ADR 0002): each Character's gain computer and detector, and its
// Colour mapping, chosen by Settings::character. Clean's pieces are in Clean.h and VCA's in Vca.h; FET (#20) and Opto
// (#21) fill in their own here, and until then play as Clean.

#include "Clean.h"
#include "Vca.h"

#include "comp1/Settings.h"

#include <plugkit/OutputLevel.h>
#include <plugkit/Units.h>

#include <cmath>

namespace comp1
{

// The Character that plays for character: FET and Opto play as Clean until they bring their own DSP.
inline Character played (Character character)
{
    return character == Character::VCA ? Character::VCA : Character::Clean;
}

// One detection part's detector, for every Character: a sample of the detection signal in, the Gain Reduction to
// apply out. It holds the Gain Reduction it is producing, and process() takes the detection sample rather than a
// level, so a Character whose detection hears its own gain (FET's feedback) reads it before the new Gain Reduction is
// known. A Character change keeps the state, so the incoming Character starts from the outgoing one's Gain Reduction.
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

    // Takes one sample of the detection signal and returns the Gain Reduction to apply, in dB. Clean reads the sample's
    // peak level, and VCA its RMS level; each feeds Clean's gain computer and detector. VCA's RMS level runs on every
    // Character, so a change to VCA starts from a settled level.
    double process (Character character, float sample, const clean::Curve& curve, const clean::Timing& timing)
    {
        const double rmsDb = rms.process (sample);
        levelDb = character == Character::VCA ? rmsDb : plugkit::gainToDecibels (std::abs (static_cast<double> (sample)));
        return detector.process (clean::gainReduction (levelDb, curve), timing);
    }

    // The Detection Level the last sample read, in dB where a full-scale sine reads 0: what the detector compared with
    // Threshold.
    double detectionLevelDb() const { return levelDb; }

private:
    clean::Detector detector;
    vca::RmsLevel rms;
    double levelDb = plugkit::levelFloorDb;
};

// A Character's Colour at a Gain Reduction in dB: the residual's weight, before the Colour control scales it, and the
// drive. Clean has none: a weight of exactly 0.
inline ColourAt colourOf (Character character, double gainReductionDb)
{
    return character == Character::VCA ? vca::colourAt (gainReductionDb) : ColourAt {};
}

} // namespace comp1
