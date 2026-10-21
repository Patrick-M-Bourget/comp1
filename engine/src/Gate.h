#pragma once

// The Gate: a hard gate on the main input, ahead of the Dry/Wet split, so it lowers the Dry and the Wet path alike, as
// dyn-lab models it (tools/dyn-lab/USAGE.md, "Gate"), with its figures (python3 tools/dyn-lab/dynlab.py gate).
//
// It listens to the Detection Source after the Detection EQ, from a detection stage of its own, fed the main input as
// the Internal signal and the Sidechain, with the compressor's Detection Source and Detection EQ settings. So it never
// listens to its own output, the compressor, or any feedback path. One Gate gain goes on every main channel, from the
// louder channel's Detection Level, whatever Stereo Placement and Stereo Link are.
//
// The held level is the loudest Detection Level over the sample and the level hold before it. The Gate opens
// when the held level reaches Gate Threshold and closes once it is the hysteresis under it. Gate Hold keeps it open for
// Gate Hold after it would close, before Gate Release begins; a held level back above the closing level restarts the
// count. Open, it asks for no Gain Reduction; closed, exactly Gate Range. Gate Attack is how fast it opens and Gate
// Release how fast it closes, each one pole in dB. With External and no Sidechain connected it is open.

#include "Clean.h"
#include "Detection.h"
#include "Glide.h"
#include "LevelTaps.h"
#include "comp1/Engine.h"

#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace comp1
{

// The loudest of the last few levels: a sliding maximum over a window of the sample and holdSamples before it, kept as
// the levels each louder than every later one, oldest first, in storage sized at prepare().
class LevelHold
{
public:
    void prepare (int newHoldSamples)
    {
        holdSamples = std::max (newHoldSamples, 0);
        entries.assign (static_cast<std::size_t> (holdSamples) + 2, {});
        reset();
    }

    void reset()
    {
        first = count = 0;
        n = 0;
    }

    // The loudest level of the window, after one more sample at levelDb.
    double next (double levelDb)
    {
        ++n;
        while (count > 0 && at (count - 1).levelDb <= levelDb)
            --count;
        at (count++) = { n, levelDb };
        if (at (0).sample < n - holdSamples)
        {
            first = (first + 1) % entries.size();
            --count;
        }
        return at (0).levelDb;
    }

private:
    struct Entry
    {
        std::int64_t sample = 0;
        double levelDb = 0.0;
    };

    Entry& at (std::size_t i) { return entries[(first + i) % entries.size()]; }

    int holdSamples = 0;
    std::vector<Entry> entries;
    std::size_t first = 0, count = 0;
    std::int64_t n = 0;
};

// The Gate stage. Its settings glide with plugkit's Smoother a step of a run at each run's start, as the compressor's
// do: Gate Threshold, Gate Range and Gate Hold as they are, Gate Attack and Gate Release in log time. So a change, Host
// Automation included, is click-free and lands the same at every host block size. Switching it on or off glides its
// Gain Reduction in or out over the glide time, a sample at a time; its detector runs either way. Settled off, or open
// with no Gain Reduction, it leaves the audio as it is, bit for bit.
class GateStage
{
public:
    // dyn-lab's figures: GATE_LEVEL_HOLD_MS, GATE_HYSTERESIS_DB and GATE_SETTLED_DB.
    static constexpr double levelHoldMs = 10.0;
    static constexpr double hysteresisDb = 6.0;
    static constexpr double settledDb = 1.0e-7;

    // Allocates for up to numChannels channels of up to a run a piece, and a stereo Sidechain.
    void prepare (double newSampleRate, int numChannels)
    {
        sampleRate = newSampleRate;
        detection.prepare (sampleRate, numChannels, plugkit::runSamples);
        levelHold.prepare (static_cast<int> (std::lround (levelHoldMs * sampleRate / 1000.0)));
        for (auto& setting : glides)
            setting.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
        share.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        share.reset (0.0);
        gainReductionDb = 0.0;
        open = true;
        held = 0;
    }

    // With snap, the settings take effect at once instead of gliding.
    void setSettings (const Settings& settings, bool snap)
    {
        detection.setSource (settings.detectionSource);
        detection.setDetectionEq (settings.detectionEq, snap);
        source = settings.detectionSource;
        const auto& gate = settings.gate;
        glideTo (glides[threshold], gate.thresholdDb, snap);
        glideTo (glides[range], gate.rangeDb, snap);
        glideTo (glides[hold], gate.holdMs, snap);
        glideTo (glides[logAttack], std::log (gate.attackMs), snap);
        glideTo (glides[logRelease], std::log (gate.releaseMs), snap);
        glideTo (share, gate.on ? 1.0 : 0.0, snap);
        if (snap)
            update();
    }

    // At the start of each run: the settings move on by a run.
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
    }

    // Gates main, a piece of the scrubbed input of at most a run, in place, listening to it and the Sidechain's piece
    // (or none). It is fed every piece in order, on or off, so its Detection EQ stays on the run grid. It notes the
    // gain it applies at each sample, its on/off share included, in levels.
    void process (AudioBlock main, const ConstAudioBlock* sidechain, RunLevels& levels)
    {
        const auto detected = detection.process ({ main.channels, main.numChannels, main.numSamples }, sidechain);
        const bool noSidechain = source == DetectionSource::External && sidechain == nullptr;
        for (int i = 0; i < main.numSamples; ++i)
        {
            double louder = 0.0;
            for (int ch = 0; ch < main.numChannels; ++ch)
                louder = std::max (louder, std::abs (static_cast<double> (detected.channelFollowedBy (ch)[i])));
            const double reductionDb = step (noSidechain, plugkit::gainToDecibels (louder));
            const double on = share.isSettled() ? share.value() : share.next();
            if (const double applied = on * reductionDb; applied > 0.0)
            {
                const double gain = plugkit::decibelsToGain (-applied);
                levels.noteGateGain (gain);
                for (int ch = 0; ch < main.numChannels; ++ch)
                    main.channels[ch][i] = static_cast<float> (gain * main.channels[ch][i]);
            }
        }
    }

private:
    // One sample of the Gate, from its Detection Level in dB (or none, with no Sidechain to listen to): its Gain
    // Reduction in dB.
    double step (bool noSidechain, double levelDb)
    {
        if (noSidechain)
        {
            open = true;
            held = 0;
        }
        else if (const double heldDb = levelHold.next (levelDb); heldDb >= thresholdDb)
        {
            open = true;
            held = 0;
        }
        else if (heldDb >= thresholdDb - hysteresisDb)
            held = 0;
        else if (open)
        {
            if (held < timing.holdSamples)
                ++held;
            else
                open = false;
        }
        const double wanted = open ? 0.0 : rangeDb;
        const double k = wanted < gainReductionDb ? timing.attackPole : timing.releasePole;
        gainReductionDb = k * gainReductionDb + (1.0 - k) * wanted;
        if (gainReductionDb < settledDb)
            gainReductionDb = 0.0;
        return gainReductionDb;
    }

    void update()
    {
        thresholdDb = glides[threshold].value();
        rangeDb = glides[range].value();
        timing = clean::timing (std::exp (glides[logAttack].value()), std::exp (glides[logRelease].value()), glides[hold].value(), sampleRate);
    }

    enum Glide : std::size_t { threshold, range, hold, logAttack, logRelease, count };

    double sampleRate = 44100.0;
    DetectionStage detection;
    DetectionSource source = DetectionSource::Internal;
    LevelHold levelHold;
    plugkit::Smoother glides[count];
    plugkit::Smoother share; // 0 off, 1 on: the share of the Gain Reduction applied
    double thresholdDb = 0.0, rangeDb = 0.0;
    clean::Timing timing; // Gate Attack's pole for opening, Gate Release's for closing, and Gate Hold
    double gainReductionDb = 0.0;
    bool open = true;
    int held = 0;
};

} // namespace comp1
