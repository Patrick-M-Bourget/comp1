#pragma once

#include "comp1/Settings.h"

#include <plugkit/OutputLevel.h>

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

// The levels of one completed run of the Engine's 16-sample grid, each in dB, the floor (plugkit::levelFloorDb) for
// silence. The level history holds one per run. Each level is the run's as its stage meets it: in High Quality, the
// Input Level is of the input as it arrives, and the others are of audio the stages before them have delayed. The Gain
// Reduction is the compressor's own, as the gain it applies, without Auto Gain or Output Gain; the Detection Level is
// what the detector compares with Threshold, in dB where a full-scale sine reads 0.
struct LevelRecord
{
    double inputDb = plugkit::levelFloorDb;          // the Input Level's sample peak, the louder channel's
    double outputDb = plugkit::levelFloorDb;         // the Output Level's sample peak, the louder channel's
    double gainReductionDb = 0.0;                    // the compressor's most Gain Reduction, the larger channel's
    double detectionLevelDb = plugkit::levelFloorDb; // the loudest Detection Level, the louder channel's
    double gateGainReductionDb = 0.0;                // the Gate's most Gain Reduction
    double limiterGainReductionDb = 0.0;             // the Limiter's most Gain Reduction
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
// The level taps: the Output Level is read with readOutputLevel(), and the level history with readLevels() and
// skipLevels(), each from one reader thread while process() runs on the audio thread. Reading never allocates, locks or does I/O, and stays safe through any prepare(), at any sample rate or
// channel count: nothing a reader touches is freed or moved while the Engine lives. Silence, anything quieter than
// plugkit::levelFloorDb, and NaN read the floor.
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
    // Saturation off (or the level below its knee: full scale at Drive 0 dB, between samples too when Oversampling
    // rebuilds them) and 0 dB Output Gain, the output is the input times the Dry and Wet levels summed: bit for bit the
    // input at Wet 0 dB and Dry Off, or at Dry 0 dB and Wet Off, and twice it at both 0 dB. A non-finite input sample
    // plays as silence, on both paths.
    //
    // The Gate comes first, on the main input before the Dry/Wet split, so it lowers the Dry and the Wet path alike: it
    // keys from the Detection Source after the Detection EQ, never from its own output, and puts one gain on every
    // channel. With External and no Sidechain connected it is open. Off, or open, it changes nothing, bit for bit.
    //
    // The Dry path is the gated input, split off before the Wet path touches it, delayed to meet the Wet path at the
    // Dry/Wet sum, and summed at its level with the Wet path, after Auto Gain, which is the Wet path's alone, and
    // before Output Gain.
    //
    // The Sidechain is heard only by the detector and the Gate, with Detection Source External: each main channel
    // detects from the Sidechain channel of its own index, a mono Sidechain is detected from by every main channel,
    // and a mono main detects from the louder of a stereo Sidechain's two channels at each sample. A Sidechain contributes its first
    // two channels. A null one, one with no channels, or one with fewer samples than main counts as none connected,
    // and the detector then hears silence. A non-finite Sidechain sample is heard as silence too.
    //
    // The Detection EQ filters what the detector hears, and Detection Audition plays, never the processed sound.
    void process (AudioBlock main, const ConstAudioBlock* sidechain = nullptr);

    // How many channels the Output Level has: one per channel the Engine was prepared with, up to two.
    int outputLevelChannels() const;

    // The Output Level of a channel (0 to outputLevelChannels() - 1): what process() sends on, after everything, so
    // after Output Gain. Its peak is the sample peak since the last read, and reading resets it, so a peak between two
    // reads is reported once, by the next; its RMS is over the last 300 ms on the run grid. Another channel reads the
    // floor.
    plugkit::OutputLevel readOutputLevel (int channel);

    // Copies up to maxRecords of the level history, oldest first, and returns how many were copied: a record per run
    // process() has completed since the last read, the same record for record however the host cuts the audio into
    // blocks. When the reader falls behind, the history holds levelHistoryCapacity() records and drops new ones.
    int readLevels (LevelRecord* destination, int maxRecords);

    // Drops what the level history holds, so the next readLevels() copies only runs completed after this: a reader
    // starting afresh, the editor opening, sees current levels rather than what piled up while nothing read them. From
    // readLevels()'s reader thread.
    void skipLevels();

    // How many records the level history holds between reads, at least 150 ms of runs: the reader's buffer is this
    // big. Safe to call from any thread.
    int levelHistoryCapacity() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace comp1
