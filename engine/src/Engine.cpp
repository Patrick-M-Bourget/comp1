#include "comp1/Engine.h"

#include "LatestValue.h"
#include "NoSubnormals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

namespace
{

// The Engine's grid: from prepare() on, the audio is cut into runs of this many samples, whatever the host's
// blocks. Settings, smoothing and filter redesigns move on it, so the output is the same at every block size.
// plugkit-dsp's run grid (ADR 0006) replaces this constant once comp1 pins the plugkit release that has it.
constexpr int runSamples = 16;

double decibelsToGain (double db) { return std::pow (10.0, db / 20.0); }

// Output Gain, applied to every channel. It takes the newest target at once, for now: plugkit's Smoother (ADR 0006)
// glides it to the target on the run grid, here, once comp1 pins the plugkit release that has it.
class OutputGain
{
public:
    void setTargetDb (double db) { gain = decibelsToGain (db); }

    void process (float* const* channels, int numChannels, int numSamples) const
    {
        // Exactly 1 at 0 dB, so the output is the input, bit for bit, with no work.
        if (gain == 1.0)
            return;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                channels[ch][i] = static_cast<float> (gain * channels[ch][i]);
    }

private:
    double gain = 1.0;
};

} // namespace

struct Engine::Impl
{
    int numChannels = 0;

    LatestValue<Settings> handoff;
    Settings settings; // the newest settings the audio thread has taken

    // Where the next sample falls in the grid of runs of runSamples samples that starts at prepare() and carries
    // across process() calls.
    int gridPosition = 0;
    std::vector<float*> subBlock; // per channel

    OutputGain outputGain;

    void applySettings()
    {
        if (const auto* latest = handoff.takeLatest())
            settings = *latest;
        outputGain.setTargetDb (settings.outputGainDb);
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double, int, int numChannels)
{
    impl->numChannels = numChannels;
    impl->gridPosition = 0;
    impl->subBlock.assign (static_cast<std::size_t> (std::max (numChannels, 0)), nullptr);
}

void Engine::setSettings (const Settings& settings)
{
    impl->handoff.publish (settings);
}

void Engine::process (AudioBlock main, const ConstAudioBlock*)
{
    const NoSubnormals noSubnormals;
    impl->applySettings();

    const int channels = std::min (main.numChannels, impl->numChannels);
    for (int start = 0, count = 0; start < main.numSamples; start += count)
    {
        // The rest of the grid's run, or of the block where it ends first, leaving the run open.
        count = std::min (runSamples - impl->gridPosition, main.numSamples - start);
        for (int ch = 0; ch < channels; ++ch)
            impl->subBlock[static_cast<std::size_t> (ch)] = main.channels[ch] + start;

        impl->outputGain.process (impl->subBlock.data(), channels, count);

        impl->gridPosition += count;
        if (impl->gridPosition == runSamples)
            impl->gridPosition = 0;
    }
}

} // namespace comp1
