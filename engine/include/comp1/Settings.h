#pragma once

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
inline constexpr ParameterRange drive { 0.0, 24.0 };        // dB; comp1's own
inline constexpr ParameterRange outputGain { -36.0, 36.0 }; // dB; comp1's own
} // namespace parameterRanges

// Where the Saturation stage sits on the Wet path. Hosts store it as a choice by index, so the order is frozen
// (ADR 0003): a new position goes at the end.
enum class SaturationPosition
{
    Before, // ahead of the compressor and its detection: the compressor reacts to the saturated signal
    After,  // after the compressor, before Auto Gain: the compressor reacts to the unsaturated signal
};

// Everything the Engine plays, in one value: the Plugin Shell builds it from the host parameters and hands it over
// whole (Engine::setSettings). A default Settings holds every host parameter's default.
struct Settings
{
    // The compressor, the Clean Character, in parameterRanges' units, with dyn-lab's defaults.
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
    // Auto Gain, on the Wet path after the compressor: estimated from Threshold, Ratio, Knee and Range.
    bool autoGain = false;
    // Saturation, on the Wet path: one soft-clip shape, Drive dB into it, level-compensated, so below the shape's knee
    // (full scale at Drive 0 dB) it passes the signal through bit for bit (dyn-lab's USAGE.md, "Saturation").
    bool saturation = false;
    double driveDb = 6.0;
    SaturationPosition saturationPosition = SaturationPosition::After;

    // Output Gain, in dB: applied to the whole output after Dry and Wet are summed, before the Limiter.
    double outputGainDb = 0.0;

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
