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
inline constexpr ParameterRange outputGain { -36.0, 36.0 }; // dB; comp1's own
inline constexpr ParameterRange lookahead { 0.0, 5.0 };     // ms; ADR 0001's budget for it
} // namespace parameterRanges

// How the whole plugin trades latency against quality (ADR 0001); comp1/Latency.h gives each one's latency. Stored in
// sessions in this order, which never changes (ADR 0003).
enum class ProcessingMode
{
    ZeroLatency,
    HighQuality,
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
    double holdMs = 0.0;
    double rangeDb = 60.0; // the most Gain Reduction the compressor applies
    // Auto Gain, on the Wet path after the compressor: estimated from Threshold, Ratio, Knee and Range.
    bool autoGain = false;

    // Output Gain, in dB: applied to the whole output after Dry and Wet are summed, before the Limiter.
    double outputGainDb = 0.0;

    // Whole-plugin: the Processing Mode, and Lookahead in ms, which High Quality alone reads.
    ProcessingMode processingMode = ProcessingMode::ZeroLatency;
    double lookaheadMs = 0.0;

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
