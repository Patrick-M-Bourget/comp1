#include "comp1/Engine.h"

#include "Clean.h"
#include "DelayLine.h"
#include "Detection.h"
#include "Lookahead.h"
#include "ModeFade.h"
#include "SaturationStage.h"
#include "comp1/Latency.h"

#include <plugkit/LatestValue.h>
#include <plugkit/NoSubnormals.h>
#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

// The compressor: the Clean Character's gain computer and detector on every sample of every channel, each main channel
// detected from the detection channel it follows and compressed on its own.
//
// Its settings glide with plugkit's Smoother, a step of a whole run at each run's start, so a change, Host Automation
// included, is click-free and glides the same at every host block size; the gain computer and detector read the
// settings as they stand for the run. Ratio glides as 1/Ratio, so ∞:1 (0) is reached; Attack and Release glide in
// log time, as their host parameters move.
class Compressor
{
public:
    void prepare (double newSampleRate, int numChannels)
    {
        sampleRate = newSampleRate;
        detectors.assign (static_cast<std::size_t> (std::max (numChannels, 0)), {});
        for (auto& detector : detectors)
            detector.prepare (sampleRate);
        for (auto& setting : glides)
            setting.configure (plugkit::glideTimeConstantSeconds * sampleRate, arrivalTolerance);
    }

    // With snap, the settings take effect at once instead of gliding.
    void setSettings (const Settings& settings, bool snap)
    {
        const double targets[] = { settings.thresholdDb, 1.0 / settings.ratio, settings.kneeDb, settings.rangeDb,
                                   std::log (settings.attackMs), std::log (settings.releaseMs), settings.holdMs };
        for (std::size_t s = 0; s < count; ++s)
        {
            if (snap)
                glides[s].reset (targets[s]);
            else
                glides[s].setTarget (targets[s]);
        }
        if (snap)
            update();
        // Auto Release switches without a glide: the switch changes only how fast the gain comes back, not where it is.
        autoRelease = settings.autoRelease;
    }

    // At the start of each run: the settings move on by a run, and the gain computer and detector take them; then
    // each detector's Auto Release moves on by the run just heard.
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
        for (auto& detector : detectors)
            detector.startRun (releaseMs, autoRelease);
    }

    // Applies gain to audio, sample for sample, from the detection signal of the same samples: each main channel's
    // Detection Level comes from the detection channel it follows, never from the audio.
    void process (const DetectionSignal& detection, AudioBlock audio)
    {
        for (int ch = 0; ch < audio.numChannels; ++ch)
        {
            auto& detector = detectors[static_cast<std::size_t> (ch)];
            const float* detected = detection.channelFollowedBy (ch);
            for (int i = 0; i < audio.numSamples; ++i)
            {
                // The Detection Level: the detection signal's peak level, in dB where a full-scale sine reads 0.
                const double levelDb = plugkit::gainToDecibels (std::abs (static_cast<double> (detected[i])));
                const double reduction = detector.process (clean::gainReduction (levelDb, curve), timing);
                float& sample = audio.channels[ch][i];
                if (reduction > 0.0)
                    sample = static_cast<float> (plugkit::decibelsToGain (-reduction) * sample);
            }
        }
    }

private:
    enum Glide : std::size_t { threshold, inverseRatio, knee, range, logAttack, logRelease, hold, count };
    // Close enough to count as arrived, in each glide's units (dB, 1/Ratio, log ms, ms): far below what can be heard.
    static constexpr double arrivalTolerance = 1.0e-7;

    void update()
    {
        curve = { .thresholdDb = glides[threshold].value(),
                  .inverseRatio = glides[inverseRatio].value(),
                  .kneeDb = glides[knee].value(),
                  .rangeDb = glides[range].value() };
        releaseMs = std::exp (glides[logRelease].value());
        timing = clean::timing (std::exp (glides[logAttack].value()), releaseMs, glides[hold].value(), sampleRate);
    }

    double sampleRate = 44100.0;
    plugkit::Smoother glides[count];
    clean::Curve curve;
    clean::Timing timing;
    double releaseMs = 100.0;
    bool autoRelease = false;
    std::vector<clean::Detector> detectors; // per channel
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
        const double target = plugkit::decibelsToGain (db);
        if (snap)
            gain.reset (target);
        else
            gain.setTarget (target);
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

} // namespace

struct Engine::Impl
{
    int numChannels = 0;

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
    // High Quality's delays (comp1/Latency.h): the Lookahead budget, around the compressor, and the reserves' padding.
    LookaheadStage lookahead;
    DelayLine oversamplingPadding, limiterPadding;
    ModeFade modeFade;

    void applySettings()
    {
        const auto* latest = handoff.takeLatest();
        if (latest == nullptr && ! snapToSettings)
            return;
        if (latest != nullptr)
            settings = *latest;
        compressor.setSettings (settings, snapToSettings);
        autoGain.setTargetDb (settings.autoGain ? clean::autoGainDb (curveOf (settings)) : 0.0, snapToSettings);
        saturation.setSettings (settings, snapToSettings);
        outputGain.setTargetDb (settings.outputGainDb, snapToSettings);
        lookahead.setLookahead (settings.lookaheadMs, snapToSettings);
        if (snapToSettings)
            modeFade.reset (settings.processingMode);
        else
            modeFade.setTarget (settings.processingMode);
        snapToSettings = false;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int, int numChannels)
{
    impl->numChannels = numChannels;
    impl->gridPosition = 0;
    impl->subBlock.assign (static_cast<std::size_t> (std::max (numChannels, 0)), nullptr);
    impl->detection.prepare (numChannels, plugkit::runSamples);
    impl->compressor.prepare (sampleRate, numChannels);
    impl->saturation.prepare (sampleRate, numChannels);
    impl->autoGain.prepare (sampleRate);
    impl->outputGain.prepare (sampleRate);
    impl->lookahead.prepare (sampleRate, numChannels, plugkit::runSamples);
    impl->oversamplingPadding.prepare (numChannels, oversamplingReserveSamples);
    impl->limiterPadding.prepare (numChannels, limiterReserveSamples (sampleRate));
    impl->modeFade.prepare (sampleRate);
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
        for (int ch = 0; ch < channels; ++ch)
        {
            float* samples = main.channels[ch] + start;
            impl->subBlock[static_cast<std::size_t> (ch)] = samples;
            // A non-finite input sample (nan, inf) never reaches the output, nor the detector, whose Gain Reduction
            // it would hold forever: it plays as silence.
            for (int i = 0; i < count; ++i)
                samples[i] = std::isfinite (samples[i]) ? samples[i] : 0.0f;
        }
        for (int ch = 0; ch < sidechainChannels; ++ch)
            sidechainPiece[static_cast<std::size_t> (ch)] = sidechain->channels[ch] + start;
        const ConstAudioBlock sidechainBlock { sidechainPiece.data(), sidechainChannels, count };

        // The Wet path: Saturation Before, the detection stage, fed the compressor's input as the Internal signal,
        // then the compressor, Saturation After, Auto Gain and Output Gain; Saturation runs at one of its two
        // positions. The compressor's settings step at the start of each run, wherever the host's blocks fall;
        // Saturation, Auto Gain and Output Gain glide a sample at a time with next(), so where the pieces fall changes
        // nothing either. From the sample a new setting is taken (the start of the host's block, so the host
        // decides when a change lands), the output is the same sample for sample however the audio is cut.
        const AudioBlock wet { impl->subBlock.data(), channels, count };
        if (impl->gridPosition == 0)
            impl->saturation.startRun();
        impl->saturation.process (SaturationPosition::Before, wet);
        const auto detected = impl->detection.process ({ wet.channels, wet.numChannels, wet.numSamples },
                                                       sidechainConnected ? &sidechainBlock : nullptr);
        if (impl->gridPosition == 0)
        {
            impl->compressor.startRun();
            impl->lookahead.startRun();
        }
        // In High Quality the audio is delayed by the Lookahead budget and the detection signal by less, so the
        // compressor meets the audio Lookahead early; then the Oversampling padding before the Dry/Wet sum, and the
        // Limiter padding. Last, the mode fade, which swaps the paths in silence.
        const bool highQuality = path == ProcessingMode::HighQuality;
        impl->compressor.process (impl->lookahead.process (detected, wet, highQuality), wet);
        impl->saturation.process (SaturationPosition::After, wet);
        impl->autoGain.process (wet);
        if (highQuality)
            impl->oversamplingPadding.process (wet);
        impl->outputGain.process (wet);
        if (highQuality)
            impl->limiterPadding.process (wet);
        impl->modeFade.process (wet);

        impl->gridPosition += count;
        if (impl->gridPosition == plugkit::runSamples)
            impl->gridPosition = 0;
    }
}

} // namespace comp1
