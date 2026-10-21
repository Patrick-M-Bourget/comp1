#pragma once

#include "comp1/Settings.h"

#include <memory>

namespace comp1
{

struct AudioBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
};

struct ConstAudioBlock
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
};

// The DSP Engine: plain C++, with no JUCE. prepare() may allocate; setSettings() and process() never allocate,
// lock or do I/O. process() flushes subnormal numbers to zero, whatever the caller's floating-point mode, and
// leaves that mode as it found it.
//
// Threads: process() runs on the audio thread. setSettings() may run on another thread, but only one thread at a
// time may call it; the newest settings are taken at the start of each process().
//
// Latency: in High Quality the output is delayed by comp1/Latency.h's latencySamples() for the sample rate, the same
// whatever the other settings; what is said of the output below holds of it delayed so. Zero Latency has none.
//
// Analysis taps: none yet. The level taps (input level, output level, Gain Reduction and Detection Level) are read
// here (#7).
class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // Every setting at once, replacing the last ones.
    void setSettings (const Settings& settings);

    // Processes main in place. sidechain may be null when nothing is connected. Blocks may be of any size, changing
    // from call to call. The output doesn't depend on how the host cuts the audio into blocks, sample for sample: the
    // Engine moves on a grid of 16-sample runs that starts at prepare() and carries across calls. While Detection
    // Audition isn't held, with no Gain Reduction (Ratio 1:1, Range 0 dB, or a level below the Knee), Auto Gain off,
    // Saturation off (or the level below its knee: full scale at Drive 0 dB) and 0 dB Output Gain, the output is the
    // input, bit for bit; a non-finite input sample plays as silence.
    //
    // The Sidechain is heard only by the detector, with Detection Source External: each main channel detects from the
    // Sidechain channel of its own index, a mono Sidechain is detected from by every main channel, and a mono main
    // detects from the louder of a stereo Sidechain's two channels at each sample. A Sidechain contributes its first
    // two channels. A null one, one with no channels, or one with fewer samples than main counts as none connected,
    // and the detector then hears silence. A non-finite Sidechain sample is heard as silence too.
    void process (AudioBlock main, const ConstAudioBlock* sidechain = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace comp1
