#pragma once

#include <plugkit/BandShape.h>
#include <plugkit/ShapeDesign.h>

#include <limits>

namespace comp1
{

// A host parameter's span, in the units the editor shows.
struct ParameterRange
{
    double low = 0.0;
    double high = 0.0;
};

// Every range the host parameters span, each named once; dyn-lab (tools/dyn-lab/dynlab.py) names the same ones,
// with their sources. Pro-C 3's, where FabFilter publishes them, and comp1's own where it doesn't (ADR 0008).
// Settings holds values within them.
namespace parameterRanges
{
inline constexpr double inf = std::numeric_limits<double>::infinity();

inline constexpr ParameterRange threshold { -60.0, 0.0 };   // dB; comp1's own
inline constexpr ParameterRange ratio { 1.0, inf };         // :1, up to ∞:1
inline constexpr ParameterRange knee { 0.0, 72.0 };         // dB
inline constexpr ParameterRange attack { 0.005, 250.0 };    // ms
inline constexpr ParameterRange release { 5.0, 5000.0 };    // ms; comp1's own
inline constexpr ParameterRange hold { 0.0, 500.0 };        // ms
inline constexpr ParameterRange range { 0.0, 60.0 };        // dB; comp1's own: its top sets no limit
inline constexpr ParameterRange colour { 0.0, 100.0 };      // %; comp1's own: none to the Character's tuned amount
inline constexpr ParameterRange drive { 0.0, 24.0 };        // dB; comp1's own
// Dry and Wet, each from Off (-inf) up. Pro-C 3's help ("Time controls") names its Dry Gain and Wet Gain knobs but
// gives no range, so these are comp1's own (ADR 0008): +12 dB of boost over unity, enough to bring a quiet parallel
// path up level with a heavily compressed one.
inline constexpr ParameterRange dry { -inf, 12.0 };         // dB; comp1's own
inline constexpr ParameterRange wet { -inf, 12.0 };         // dB; comp1's own
inline constexpr ParameterRange outputGain { -36.0, 36.0 }; // dB; comp1's own
inline constexpr ParameterRange lookahead { 0.0, 5.0 };     // ms; ADR 0001's budget for it
inline constexpr ParameterRange stereoLink { 0.0, 100.0 };  // %
inline constexpr ParameterRange ceiling { -30.0, 0.0 };     // dBFS; comp1's own, with dyn-lab's reasons (CEILING_RANGE)
// The Detection EQ's, which dyn-lab doesn't model. Pro-C 3's manual gives its side chain filters' Slopes up to
// 96 dB/oct ("Side chain section"), and no other range of its side chain EQ, so the rest are comp1's own (ADR 0008).
inline constexpr ParameterRange detectionFrequency { 10.0, 30000.0 }; // Hz; comp1's own: 20 Hz to 20 kHz with room each side
inline constexpr ParameterRange detectionSlope { 6.0, 96.0 };         // dB/oct, in 6 dB/oct steps; the top Pro-C 3's
inline constexpr ParameterRange detectionGain { -30.0, 30.0 };        // dB; comp1's own: plugkit's designs go no further
inline constexpr ParameterRange detectionQ { 0.025, 40.0 };           // comp1's own: from a wide tilt to a narrow peak
} // namespace parameterRanges

// The Characters: the four Styles that carry DSP (ADR 0002), in Style's order, Style's first four, so frozen with it
// (ADR 0003). The Engine always plays a Character, never a Style: the Plugin Shell maps Style to one (#24). FET and
// Opto play as Clean until they bring their own DSP (#20, #21).
enum class Character
{
    Clean,
    VCA,
    FET,
    Opto,
};

// Where the Saturation stage sits on the Wet path. Hosts store it as a choice by index, so the order is frozen
// (ADR 0003): a new position goes at the end.
enum class SaturationPosition
{
    Before, // ahead of the compressor and its detection: the compressor reacts to the saturated signal
    After,  // after the compressor, before Auto Gain: the compressor reacts to the unsaturated signal
};

// What the compressor's detection listens to: the main input (Internal), or the Sidechain (External). Saved by index,
// so the order is frozen.
enum class DetectionSource
{
    Internal,
    External,
};

// The Detection EQ's free band's Shapes. Saved by index, so the order is frozen.
enum class FreeBandShape
{
    Bell,
    LowShelf,
    HighShelf,
};

// One Detection EQ band: whether it is on, and its filter, as plugkit's designs read it. Off, it passes the detection
// signal unchanged and costs nothing.
struct DetectionBand
{
    bool on = false;
    plugkit::BandShape filter;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal" // exactly equal: settings that changed nothing are skipped
#endif
    bool operator== (const DetectionBand& other) const
    {
        const auto& a = filter;
        const auto& b = other.filter;
        return on == other.on && a.shape == b.shape && a.slope == b.slope && a.brickwall == b.brickwall && a.gain == b.gain
               && a.frequency == b.frequency && a.q == b.q;
    }
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
};

// The Detection EQ: a high-pass, a low-pass and one free band, shaping the detection signal only, never the processed
// sound. Each is off by default. The cuts are plain Butterworth (Q 0.71), with no Brickwall; the free band's shelves
// are second-order (12 dB/oct).
struct DetectionEq
{
    DetectionBand highPass { .filter = { .shape = plugkit::Shape::LowCut, .slope = 12.0, .frequency = 20.0, .q = plugkit::butterworthQ } };
    DetectionBand lowPass { .filter = { .shape = plugkit::Shape::HighCut, .slope = 12.0, .frequency = 20000.0, .q = plugkit::butterworthQ } };
    DetectionBand freeBand { .filter = { .shape = plugkit::Shape::Bell, .slope = 12.0, .gain = 0.0, .frequency = 1000.0, .q = 1.0 } };

    bool operator== (const DetectionEq&) const = default;
};

// The plugkit Shape a free band's Shape designs as.
inline plugkit::Shape shapeOf (FreeBandShape shape)
{
    switch (shape)
    {
        case FreeBandShape::Bell: return plugkit::Shape::Bell;
        case FreeBandShape::LowShelf: return plugkit::Shape::LowShelf;
        case FreeBandShape::HighShelf: return plugkit::Shape::HighShelf;
    }
    return plugkit::Shape::Bell;
}

// How the whole plugin trades latency against quality (ADR 0001); comp1/Latency.h gives each one's latency. Stored in
// sessions in this order, which never changes (ADR 0003).
enum class ProcessingMode
{
    ZeroLatency,
    HighQuality,
};

// Which part of the stereo signal the compressor compresses: both channels, or the Mid or the Side alone. comp1's own
// list, stored in sessions in this order, which never changes once released; the Engine maps it onto plugkit's
// StereoPlacement where it routes.
enum class StereoPlacement
{
    Stereo,
    Mid,
    Side,
};

// Everything the Engine plays, in one value: the Plugin Shell builds it from the host parameters and hands it over
// whole (Engine::setSettings). A default Settings holds every host parameter's default.
struct Settings
{
    // The Gate, first in the chain, on the main input before the Dry/Wet split: on or off, Gate Threshold and Gate
    // Range in dB, Gate Attack, Gate Release and Gate Hold in ms, in the compressor's ranges of the same names
    // (parameterRanges), with dyn-lab's defaults (its Gate). It keys from the Detection Source after the Detection EQ.
    struct Gate
    {
        bool on = false;
        double thresholdDb = -40.0;
        double rangeDb = 20.0;
        double attackMs = 1.0;
        double releaseMs = 100.0;
        double holdMs = 20.0;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal" // exactly equal: settings that changed nothing are skipped
#endif
        bool operator== (const Gate&) const = default;
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
    };
    Gate gate;

    // The compressor's Character: how it detects, and its Colour. Not a host parameter: Style sets it (#24).
    Character character = Character::Clean;
    // The compressor, in parameterRanges' units, with dyn-lab's defaults.
    double thresholdDb = -18.0;
    double ratio = 4.0; // parameterRanges::inf for ∞:1
    double kneeDb = 6.0;
    double attackMs = 10.0;
    double releaseMs = 100.0;
    // Auto Release: Release lengthens after sustained compression, up to 5 times as long, and stays short after
    // transients (dyn-lab's USAGE.md, "Auto Release").
    bool autoRelease = false;
    double holdMs = 0.0;
    double rangeDb = 60.0; // the most Gain Reduction the compressor applies
    // Colour, in % (0 to 100): how much of its Character's own distortion, which grows with the Gain Reduction, the
    // compressor adds (dyn-lab's USAGE.md, "Colour"). Clean has none, at any Colour.
    double colour = 100.0;
    // Auto Gain, on the Wet path after the compressor: estimated from Threshold, Ratio, Knee and Range.
    bool autoGain = false;
    // Saturation, on the Wet path: one soft-clip shape, Drive dB into it, level-compensated, so below the shape's knee
    // (full scale at Drive 0 dB) it passes the signal through bit for bit (dyn-lab's USAGE.md, "Saturation").
    bool saturation = false;
    double driveDb = 6.0;
    SaturationPosition saturationPosition = SaturationPosition::After;

    // Dry and Wet, in dB, -parameterRanges::inf for Off: the levels the Dry path (the input, delayed to match the Wet
    // path) and the Wet path are summed at, before Output Gain. The defaults, Dry Off and Wet 0 dB, play the Wet path
    // alone, bit for bit.
    double dryDb = -parameterRanges::inf;
    double wetDb = 0.0;

    // Output Gain, in dB: applied to the whole output after Dry and Wet are summed, before the Limiter.
    double outputGainDb = 0.0;
    // The Limiter, last, after Output Gain: on or off, and its Ceiling in dBFS, the highest level it lets through,
    // as the sample peak in Zero Latency and the true peak in High Quality.
    bool limiter = false;
    double ceilingDb = 0.0;

    // Detection Source: what the detection stage feeds the compressor's detector.
    DetectionSource detectionSource = DetectionSource::Internal;
    // The Detection EQ, which filters the detection source's channels before the detector hears them.
    DetectionEq detectionEq;

    // Where on a stereo track the compressor works: the part Stereo Placement picks, and with Stereo, Stereo Link in %
    // (0 to 100), how much the two channels share one Gain Reduction, from each channel's own at 0% to the larger of
    // the two at 100% (comp1's own default, ADR 0008). A mono track is compressed as Stereo, whatever they hold.
    StereoPlacement stereoPlacement = StereoPlacement::Stereo;
    double stereoLink = 100.0;

    // Detection Audition: not a host parameter, and never saved. Held while the editor holds it, it plays the
    // detection signal in place of the Dry and Wet sum, before Output Gain.
    bool detectionAudition = false;

    // Whole-plugin: the Processing Mode, and Lookahead in ms, which High Quality alone reads.
    ProcessingMode processingMode = ProcessingMode::ZeroLatency;
    double lookaheadMs = 0.0;
    // Global Bypass: on, the output crossfades to the input, delayed by the latency of the Processing Mode playing
    // (comp1/Latency.h), over the Smoother's glide time; off, back to the processed output. The Engine processes
    // underneath while it is on. Separate from the host's own bypass, which the Plugin Shell plays through it.
    bool globalBypass = false;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal" // exactly equal: settings that changed nothing are skipped
#endif
    bool operator== (const Settings&) const = default;
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
};

} // namespace comp1
