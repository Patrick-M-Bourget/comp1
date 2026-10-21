#pragma once

namespace comp1
{

// A host parameter's span, in the units the editor shows.
struct ParameterRange
{
    double low = 0.0;
    double high = 0.0;
};

// Every range the host parameters span. Settings holds the same values.
namespace parameterRanges
{
inline constexpr ParameterRange outputGain { -36.0, 36.0 }; // dB
} // namespace parameterRanges

// Everything the Engine plays, as one snapshot: the Plugin Shell builds it from the host parameters and hands it
// over whole (Engine::setSettings). Later tickets add the compressor's settings here.
struct Settings
{
    // Output Gain, in dB: applied to the whole output after Dry and Wet are summed, before the Limiter.
    double outputGainDb = 0.0;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal" // exactly equal: a snapshot that changed nothing is skipped
#endif
    bool operator== (const Settings&) const = default;
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
};

} // namespace comp1
