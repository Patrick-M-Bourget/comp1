#include "comp1/Engine.h"

#include "Clean.h"

#include <plugkit/LatestValue.h>
#include <plugkit/NoSubnormals.h>
#include <plugkit/RunGrid.h>
#include <plugkit/Smoother.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace comp1
{

namespace
{

// The compressor: the Clean Character's gain computer on every sample of every channel, each channel on its own.
class Compressor
{
public:
    void setSettings (const Settings& settings)
    {
        curve = { .thresholdDb = settings.thresholdDb,
                  .inverseRatio = 1.0 / settings.ratio,
                  .kneeDb = settings.kneeDb,
                  .rangeDb = settings.rangeDb };
    }

    void process (AudioBlock block) const
    {
        for (int ch = 0; ch < block.numChannels; ++ch)
            for (int i = 0; i < block.numSamples; ++i)
            {
                float& sample = block.channels[ch][i];
                // The Detection Level: the main input's own peak level, in dB where a full-scale sine reads 0.
                const double levelDb = plugkit::gainToDecibels (std::abs (static_cast<double> (sample)));
                const double reduction = clean::gainReduction (levelDb, curve);
                if (reduction > 0.0)
                    sample = static_cast<float> (plugkit::decibelsToGain (-reduction) * sample);
            }
    }

private:
    clean::Curve curve;
};

// Output Gain, applied to every channel. Its gain glides to each new target with plugkit's Smoother, so a change,
// Host Automation included, is click-free.
class OutputGain
{
public:
    void prepare (double sampleRate)
    {
        gain.configure (plugkit::glideTimeConstantSeconds * sampleRate, 1.0e-6);
        gain.reset (1.0);
    }

    // With snap, the new gain takes effect at once instead of gliding.
    void setTargetDb (double db, bool snap)
    {
        const double target = plugkit::decibelsToGain (db);
        if (snap)
            gain.reset (target);
        else
            gain.setTarget (target);
    }

    void process (AudioBlock block)
    {
        // Settled on exactly 1 at 0 dB, so the output is the input, bit for bit, with no work.
        if (gain.isSettled() && gain.value() == 1.0)
            return;
        // The gain glides a sample at a time; once it has settled, next() would return its goal exactly, so the rest
        // is scaled by that as a constant, sample for sample the same.
        int i = 0;
        for (; i < block.numSamples && ! gain.isSettled(); ++i)
        {
            const double g = gain.next();
            for (int ch = 0; ch < block.numChannels; ++ch)
                block.channels[ch][i] = static_cast<float> (g * block.channels[ch][i]);
        }
        const double g = gain.value();
        for (int ch = 0; ch < block.numChannels; ++ch)
            for (int n = i; n < block.numSamples; ++n)
                block.channels[ch][n] = static_cast<float> (g * block.channels[ch][n]);
    }

private:
    plugkit::Smoother gain;
};

} // namespace

struct Engine::Impl
{
    int numChannels = 0;

    plugkit::LatestValue<Settings> handoff;
    Settings settings;          // the newest settings the audio thread has taken
    bool snapToSettings = true; // after prepare, settings apply at once instead of gliding

    // Where the next sample falls in the grid of runs of plugkit::runSamples samples that starts at prepare() and
    // carries across process() calls.
    int gridPosition = 0;
    std::vector<float*> subBlock; // per channel

    Compressor compressor;
    OutputGain outputGain;

    void applySettings()
    {
        const auto* latest = handoff.takeLatest();
        if (latest == nullptr && ! snapToSettings)
            return;
        if (latest != nullptr)
            settings = *latest;
        compressor.setSettings (settings);
        outputGain.setTargetDb (settings.outputGainDb, snapToSettings);
        snapToSettings = false;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int, int numChannels)
{
    impl->numChannels = numChannels;
    impl->gridPosition = 0;
    impl->subBlock.assign (static_cast<std::size_t> (std::max (numChannels, 0)), nullptr);
    impl->outputGain.prepare (sampleRate);
    impl->snapToSettings = true;
}

void Engine::setSettings (const Settings& settings)
{
    impl->handoff.publish (settings);
}

void Engine::process (AudioBlock main, const ConstAudioBlock*)
{
    const plugkit::NoSubnormals noSubnormals;
    impl->applySettings();

    const int channels = std::min (main.numChannels, impl->numChannels);
    for (int start = 0, count = 0; start < main.numSamples; start += count)
    {
        // The rest of the grid's run, or of the block where it ends first, leaving the run open.
        count = std::min (plugkit::runSamples - impl->gridPosition, main.numSamples - start);
        for (int ch = 0; ch < channels; ++ch)
            impl->subBlock[static_cast<std::size_t> (ch)] = main.channels[ch] + start;

        impl->compressor.process ({ impl->subBlock.data(), channels, count });
        // The grid cuts the block into pieces, but Output Gain glides a sample at a time with next(), not a step per
        // run, so where the pieces fall changes nothing: from the sample a new setting is taken (the start of the
        // host's block, so the host decides when a change lands), the gain is the same sample for sample however
        // the audio is cut.
        impl->outputGain.process ({ impl->subBlock.data(), channels, count });

        impl->gridPosition += count;
        if (impl->gridPosition == plugkit::runSamples)
            impl->gridPosition = 0;
    }
}

} // namespace comp1
