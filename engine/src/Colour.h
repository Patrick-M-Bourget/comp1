#pragma once

// Colour: a Character's own distortion, which grows with its Gain Reduction (dyn-lab's USAGE.md, "Colour"). The
// saturation module's shape and anti-aliased residual (Saturation.h), unchanged, driven by the Character's mapping of
// the Gain Reduction applied, sample by sample. The compressor works out each voice's Colour controls and applies only
// its gain; Colour itself runs in the After section, on the compressed signal, ahead of Saturation After (ADR 0009):
// in High Quality at the Oversampling in force, in Zero Latency at the host rate, so both modes share one formula.

#include "Saturation.h"

#include "comp1/Settings.h"

#include <plugkit/Placement.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

// A Character's Colour at a Gain Reduction: the residual's weight, and the drive in dB the compressed signal is shaped
// at, the tracking of Threshold included.
struct ColourAt
{
    double weight = 0.0;
    double driveDb = 0.0;
};

// dyn-lab's COLOUR_THRESHOLD_REFERENCE_DB: the Threshold at which Colour's shape is driven by the mapping's drive
// alone. It is the default Threshold, so every figure at the defaults is the drive's alone.
inline const double colourThresholdReferenceDb = Settings {}.thresholdDb;

// A Character's Colour mapping (dyn-lab's COLOUR_MAPPINGS): the most its weight reaches, and the Gain Reductions in dB
// over which its weight and its drive rise.
struct ColourMapping
{
    double weight = 0.0;
    double weightDb = 1.0;
    double driveDb = 1.0;
};

// The Colour mapping at a Gain Reduction and a Threshold in dB, before the Colour control scales its weight: the weight
// mapping.weight × (1 - e^(-GR / mapping.weightDb)), and the drive Drive's top × (1 - e^(-GR / mapping.driveDb)),
// which nears it and never passes it, plus the tracking, colourThresholdReferenceDb less Threshold. The weight is
// exactly 0 with no Gain Reduction, and each rises smoothly with it.
//
// The tracking drives the shape relative to Threshold: at a steady Gain Reduction and Ratio the compressed signal sits
// a fixed distance from Threshold, so its Colour is the same at every Threshold (dyn-lab's USAGE.md, "Colour").
inline ColourAt colourAt (const ColourMapping& mapping, double gainReductionDb, double thresholdDb)
{
    if (! (gainReductionDb > 0.0))
        return {};
    return { .weight = mapping.weight * -std::expm1 (-gainReductionDb / mapping.weightDb),
             .driveDb = parameterRanges::drive.high * -std::expm1 (-gainReductionDb / mapping.driveDb)
                        + (colourThresholdReferenceDb - thresholdDb) };
}

// One voice's Colour controls over a piece of the Wet path, which the compressor hands the After section: its
// Placement, and per part it compresses (each channel under Stereo, the one part under Mid or Side), at each sample,
// the residual's weight (the Colour control and the voice's share of any fade included) and the drive's gain.
struct ColourVoice
{
    plugkit::StereoPlacement placement = plugkit::StereoPlacement::Stereo;
    bool active = false; // the voice plays in the piece: the one heard, or the one fading out
    bool starts = false; // the voice starts with the piece, so its Colour starts afresh
    bool heard = false;  // some weight in the piece isn't exactly 0
    std::vector<std::vector<double>> weights, gains; // per part, per sample of the piece
};

// Every voice's Colour controls over a piece: the voice heard, and the one fading out during a Placement or Character
// change.
struct ColourControls
{
    std::array<ColourVoice, 2> voices;

    // Allocates for up to numChannels parts of up to maxSamples samples.
    void prepare (int numChannels, int maxSamples)
    {
        for (auto& voice : voices)
        {
            voice.weights.assign (static_cast<std::size_t> (std::max (numChannels, 0)), std::vector<double> (static_cast<std::size_t> (std::max (maxSamples, 0))));
            voice.gains = voice.weights;
            voice.active = voice.starts = voice.heard = false;
        }
    }

    // Whether Colour adds anything to the piece.
    bool heard() const { return std::any_of (voices.begin(), voices.end(), [] (const ColourVoice& v) { return v.active && v.heard; }); }
};

// Colour on one part (a channel, or the Mid or the Side). Each compressed sample y gains w r(g y) / g, with g the
// drive's gain and r the residual anti-aliased (ADR 0005), so the linear part, y, passes sample-aligned. With a weight
// of exactly 0 it adds exactly nothing, while the ADAA state follows the driven signal, so Colour rising has no
// transient. Never allocates.
class ColourPart
{
public:
    // Starts afresh: the next sample's ADAA takes it as its last.
    void reset() { fresh = true; }

    // What Colour adds to the compressed sample at weight and gain.
    double change (double compressed, double weight, double gain)
    {
        const double u = gain * compressed;
        if (fresh || weight == 0.0)
        {
            follow (u);
            if (weight == 0.0)
                return 0.0;
        }
        return weight * residual.process (u) / gain;
    }

    // Takes the driven sample u as the last without adding anything.
    void follow (double u)
    {
        residual.follow (u);
        fresh = false;
    }

private:
    saturation::AntiAliased residual;
    bool fresh = true;
};

// Colour in the After section: from each voice's controls, the residual Colour adds to the section's input, the
// compressed Wet signal, at the host rate or oversampled. Per voice and per part, with ADAA state of its own: under
// Stereo, each channel; under Mid or Side, that part encoded from Left and Right, its residual added as the part is
// placed (to both channels for Mid; to Left, and taken from Right, for Side), so the other part passes. During a fade
// each voice's residual is worked out on the same compressed signal, weighted by its share. Never allocates.
class ColourStage
{
public:
    // Allocates for up to numChannels channels of up to maxSamples samples at the highest rate it runs at. Each part
    // starts after silence, as in High Quality, where the Wet path's delays hold silence first, so both Processing
    // Modes start alike.
    void prepare (int numChannels, int maxSamples)
    {
        const auto channels = static_cast<std::size_t> (std::max (numChannels, 0));
        for (auto& voice : parts)
        {
            voice.assign (channels, ColourPart {});
            for (auto& part : voice)
                part.follow (0.0);
        }
        added.assign (channels, std::vector<double> (static_cast<std::size_t> (std::max (maxSamples, 0))));
        pointers.assign (channels, nullptr);
        for (std::size_t ch = 0; ch < channels; ++ch)
            pointers[ch] = added[ch].data();
    }

    // At a piece's start: a voice that starts there starts afresh.
    void startPiece (const ColourControls& controls)
    {
        for (std::size_t v = 0; v < parts.size(); ++v)
            if (controls.voices[v].starts)
                for (auto& part : parts[v])
                    part.reset();
    }

    // The residual on oversampled, host's numSamples samples at factor samples each, numChannels channels, into
    // residuals(): each voice's at each part, added. Call only when controls.heard().
    void residual (const ColourControls& controls, const double* const* oversampled, int numChannels, int numSamples, int factor)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill_n (added[static_cast<std::size_t> (ch)].begin(), numSamples * factor, 0.0);
        for (std::size_t v = 0; v < parts.size(); ++v)
        {
            const auto& voice = controls.voices[v];
            if (! voice.active)
                continue;
            if (! voice.heard)
            {
                follow (v, voice, oversampled, numChannels, numSamples * factor - 1, numSamples - 1);
                continue;
            }
            if (voice.placement == plugkit::StereoPlacement::Stereo)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto c = static_cast<std::size_t> (ch);
                    auto& part = parts[v][c];
                    const double* in = oversampled[ch];
                    double* out = pointers[c];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        const auto n = static_cast<std::size_t> (i);
                        const double weight = voice.weights[c][n], gain = voice.gains[c][n];
                        for (int k = i * factor; k < (i + 1) * factor; ++k)
                            out[k] += part.change (in[k], weight, gain);
                    }
                }
                continue;
            }
            const bool mid = voice.placement == plugkit::StereoPlacement::Mid;
            auto& part = parts[v][0];
            for (int i = 0; i < numSamples; ++i)
            {
                const auto n = static_cast<std::size_t> (i);
                const double weight = voice.weights[0][n], gain = voice.gains[0][n];
                for (int k = i * factor; k < (i + 1) * factor; ++k)
                {
                    const double r = part.change (encoded (mid, oversampled[0][k], oversampled[1][k]), weight, gain);
                    pointers[0][k] += r;
                    pointers[1][k] += mid ? r : -r;
                }
            }
        }
    }

    // When Colour adds nothing to a piece of the host's samples, its ADAA state follows them, as if it had run.
    void follow (const ColourControls& controls, const float* const* host, int numChannels, int numSamples)
    {
        if (numSamples == 0)
            return;
        for (std::size_t v = 0; v < parts.size(); ++v)
            if (controls.voices[v].active)
                follow (v, controls.voices[v], host, numChannels, numSamples - 1, numSamples - 1);
    }

    // Each channel's residual, from the last residual().
    const double* const* residuals() const { return pointers.data(); }

private:
    // Mid or Side from Left and Right, as plugkit encodes them (Placement.h), in double at the oversampled rate.
    static double encoded (bool mid, double left, double right) { return mid ? 0.5 * (left + right) : 0.5 * (left - right); }

    // Voice v's parts follow sample at of samples (host or oversampled), driven at the gain of host sample i.
    template <typename Sample>
    void follow (std::size_t v, const ColourVoice& voice, const Sample* const* samples, int numChannels, int at, int i)
    {
        const auto n = static_cast<std::size_t> (i);
        if (voice.placement == plugkit::StereoPlacement::Stereo)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto c = static_cast<std::size_t> (ch);
                parts[v][c].follow (voice.gains[c][n] * samples[ch][at]);
            }
            return;
        }
        const bool mid = voice.placement == plugkit::StereoPlacement::Mid;
        parts[v][0].follow (voice.gains[0][n] * encoded (mid, samples[0][at], samples[1][at]));
    }

    std::array<std::vector<ColourPart>, 2> parts; // per voice, per part
    std::vector<std::vector<double>> added;       // per channel, the residual
    std::vector<double*> pointers;                // into added
};

} // namespace comp1
