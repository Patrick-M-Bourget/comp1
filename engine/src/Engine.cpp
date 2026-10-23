#include "comp1/Engine.h"

#include "Audition.h"
#include "Colour.h"
#include "Compressor.h"
#include "Detection.h"
#include "DryWet.h"
#include "Gate.h"
#include "Glide.h"
#include "GlidingGain.h"
#include "GlobalBypass.h"
#include "LevelTaps.h"
#include "Limiter.h"
#include "Lookahead.h"
#include "ModeFade.h"
#include "OversampledSection.h"
#include "SaturationStage.h"
#include "SidechainDelay.h"
#include "comp1/Latency.h"

#include <plugkit/LatestValue.h>
#include <plugkit/NoSubnormals.h>
#include <plugkit/OutputLevelTap.h>
#include <plugkit/RunGrid.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

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
    SidechainDelay sidechainDelay; // the Before section's, for the detection stage
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
        // The Sidechain as the detection stage hears it: in High Quality delayed by the Before section's delay, as the
        // audio is.
        const auto* sidechainHeard = sidechainDelay.process (sidechainPiece, wet.numSamples, highQuality);
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
    impl->sidechainDelay.prepare (oversamplingSectionSamples);
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
