#pragma once

// The seam between the compressor and its Characters (ADR 0002). Each Character is described once, in characters
// below: how it reads its Detection Level, whether its detection hears its own gain, whether it steps with Opto's
// timing, and its Colour mapping. The compressor and the detector read the description and never switch on the
// Character. Clean's pieces are in Clean.h, VCA's in Vca.h, FET's in Fet.h and Opto's in Opto.h.

#include "Clean.h"
#include "Colour.h"
#include "Fet.h"
#include "Opto.h"
#include "Vca.h"

#include "comp1/Settings.h"

#include <plugkit/OutputLevel.h>
#include <plugkit/Units.h>

#include <array>
#include <cmath>
#include <cstddef>

namespace comp1
{

// What makes a Character itself, for the compressor.
struct CharacterDescription
{
    // Whether its Detection Level is VCA's RMS level (vca::RmsLevel) rather than each sample's peak.
    bool readsRms = false;
    // Whether, with an Internal Detection Source, its detection hears the gain it applies: FET's loop, solved within
    // the sample (fet::step). With an External Detection Source no Character is fed back.
    bool fedBack = false;
    // Whether it steps with Opto's timing (opto::TwoStage): an attack scaled by the overshoot and a release in two
    // parts, the slow one slowing with Opto's history. Otherwise each step is one pole, Attack's or Release's.
    bool opticalTiming = false;
    // Its Colour mapping, or none: no Colour at any setting.
    const ColourMapping* colour = nullptr;
};

// Every Character's description, in Settings' Character order.
inline constexpr std::array<CharacterDescription, 4> characters {
    CharacterDescription {}, // Clean: a peak Detection Level, feedforward, no Colour
    CharacterDescription { .readsRms = true, .colour = &vca::colour },
    CharacterDescription { .fedBack = true, .colour = &fet::colour },
    CharacterDescription { .fedBack = true, .opticalTiming = true, .colour = &opto::colour },
};

inline const CharacterDescription& describe (Character character) { return characters[static_cast<std::size_t> (character)]; }

// One detection part's detector, for every Character: a sample of the detection signal in, the Gain Reduction to
// apply out. It holds the Gain Reduction it is producing, and process() takes the detection sample rather than a
// level, so a Character whose detection hears its own gain solves its loop with it. A Character change keeps the
// state, so the incoming Character starts from the outgoing one's Gain Reduction.
class CharacterDetector
{
public:
    void prepare (double sampleRate)
    {
        detector.prepare (sampleRate);
        rms.prepare (sampleRate);
        optical.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        detector.reset();
        rms.reset();
        optical.reset();
        levelDb = plugkit::levelFloorDb;
    }

    // At the start of each run: Auto Release and Opto's history move on by the run just heard.
    void startRun (double releaseMs, bool autoReleaseOn)
    {
        detector.startRun (releaseMs, autoReleaseOn);
        optical.startRun();
    }

    // Takes one sample of the detection signal and returns the Gain Reduction to apply, in dB, as character describes
    // it. The Detection Level is the sample's peak level, or VCA's RMS level; either feeds Clean's gain computer and
    // detector. The RMS level runs on every Character, so a change to one that reads it starts from a settled level, and
    // so does Opto's history, so a change to Opto starts from what the material has been.
    //
    // A fed-back Character with an Internal Detection Source is FET's loop: its gain computer reads the detection
    // sample times the gain applied at that same sample, the level less the Gain Reduction, so each step of the detector
    // is the loop solved within the sample (fet::step). The detection signal is the compressor's input as the Wet path
    // delays it for the detector, so in Zero Latency that is the output's level, and in High Quality the undelayed
    // input's times the gain, Lookahead ahead of the delayed audio (ADR 0002). With an External Detection Source the
    // gain never reaches the Sidechain, so every Character is feedforward.
    //
    // On a Character with Opto's timing each step is Opto's two parts (opto::TwoStage::step), fed back the same way;
    // Hold and the branching between Attack and Release are Clean's detector's still.
    double process (const CharacterDescription& character, DetectionSource source, float sample, const clean::Curve& curve, const clean::Timing& timing)
    {
        const double rmsDb = rms.process (sample);
        levelDb = character.readsRms ? rmsDb : plugkit::gainToDecibels (std::abs (static_cast<double> (sample)));
        const double wantedDb = clean::gainReduction (levelDb, curve);
        optical.hear (wantedDb > 0.0);
        const bool fedBack = character.fedBack && source == DetectionSource::Internal;
        if (character.opticalTiming)
            return detector.process (wantedDb, timing, [&] (double previousDb, double pole) { return optical.step (previousDb, levelDb, wantedDb, pole, curve, fedBack); });
        optical.leave();
        if (fedBack)
            return detector.process (wantedDb, timing, [&] (double previousDb, double pole) { return fet::step (previousDb, levelDb, wantedDb, pole, curve); });
        return detector.process (wantedDb, timing);
    }

    // The Detection Level the last sample read, in dB where a full-scale sine reads 0: the detection signal's level,
    // before any feedback.
    double detectionLevelDb() const { return levelDb; }

private:
    clean::Detector detector;
    vca::RmsLevel rms;
    opto::TwoStage optical;
    double levelDb = plugkit::levelFloorDb;
};

// A Character's Colour at a Gain Reduction and a Threshold, in dB: the residual's weight, before the Colour control
// scales it, and the drive, which tracks Threshold (colourAt). A Character with no Colour has a weight of exactly 0.
inline ColourAt colourOf (const CharacterDescription& character, double gainReductionDb, double thresholdDb)
{
    return character.colour != nullptr ? colourAt (*character.colour, gainReductionDb, thresholdDb) : ColourAt {};
}

} // namespace comp1
