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
// Analysis taps: none yet. The level taps (input level, output level, Gain Reduction and Detection Level) are
// read here once the Engine publishes them.
class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    // A full snapshot of every setting, replacing the last one.
    void setSettings (const Settings& settings);

    // Processes main in place. sidechain may be null when nothing is connected. Blocks may be of any size, changing
    // from call to call. The output doesn't depend on how the host cuts the
    // audio into blocks, sample for sample: the Engine moves on a grid of 16-sample runs that starts at prepare()
    // and carries across calls. At 0 dB Output Gain the output is the input, bit for bit.
    void process (AudioBlock main, const ConstAudioBlock* sidechain = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace comp1
