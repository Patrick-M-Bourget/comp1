#pragma once

// The Expanded View open with signal playing through the processor: what the tests of the editor's level displays (the
// meters, the Detection Level ring, the Level and Gain Reduction Graph) share.

#include "EditorHost.h"
#include "plugkit/ui/AssumedSampleRate.h"

#include <cmath>
#include <functional>

namespace host
{

// A signal is a function of n, the nth sample played.

// A 1 kHz sine at sampleRate, at amplitude.
inline auto sine (double amplitude, double sampleRate = 48000.0)
{
    return [amplitude, sampleRate] (int n) { return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / sampleRate); };
}

inline auto silence()
{
    return [] (int) { return 0.0; };
}

// A steady level of levelDb, as the Engine's tests play one (Measure.h's steadyLevels): every sample at its magnitude,
// alternating in sign, which Clean's peak detection reads as levelDb without the ripple a sine's would have.
inline auto steady (double levelDb)
{
    const double magnitude = std::pow (10.0, levelDb / 20.0);
    return [magnitude] (int n) { return n % 2 == 0 ? magnitude : -magnitude; };
}

// The editor open on a stereo track at sampleRate (48 kHz unless given), with the Sidechain given (none unless given),
// compressing nothing until told: Comp Follow off.
struct Playing : EditorHost
{
    explicit Playing (double sampleRate = 48000.0, const juce::AudioChannelSet& sidechain = juce::AudioChannelSet::disabled())
    {
        editor.reset();
        harness::useLayout (processor, juce::AudioChannelSet::stereo(), sidechain);
        // As a host does: the rate the processor reports (getSampleRate), then the prepare.
        processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        reopen();
        edit (comp1::parameters::compFollowId, 0.0f);
    }

    double sampleRate() const { return plugkit::ui::sampleRateOrAssumed (processor); }

    // Plays signal for seconds of blocks of blockSize, letting the editor's tick read between every few blocks, as a
    // host's audio thread and the message thread take turns.
    template <typename Signal>
    void playFor (double seconds, Signal&& signal)
    {
        // Four blocks at 48 kHz between ticks, about 43 ms at any rate: well within the 150 ms the level history holds
        // unread.
        const int perTick = std::max (1, static_cast<int> (std::lround (sampleRate() / 12000.0)));
        const int blocks = static_cast<int> (std::ceil (seconds * sampleRate() / blockSize));
        for (int b = 0, n = 0; b < blocks; b += perTick, n += perTick * blockSize)
        {
            play (std::min (perTick, blocks - b), blockSize, [&signal, n] (int i) { return signal (n + i); });
            settle (16);
        }
    }

    // Keeps playing signal, a block at a time with the editor's tick between, until done() holds (harness::settleUntil).
    template <typename Signal>
    bool keepPlayingUntil (Signal&& signal, const std::function<bool()>& done)
    {
        int n = 0;
        return settleUntil ([&] {
            play (1, blockSize, [&signal, n] (int i) { return signal (n + i); });
            n += blockSize;
            return done();
        });
    }

    comp1::GainReductionMeter& meter() { return expandedView().gainReductionMeter(); }
    comp1::GainReductionMeter& gateMeter() { return expandedView().gateGainReductionMeter(); }
    comp1::GainReductionMeter& limiterMeter() { return expandedView().limiterGainReductionMeter(); }
    comp1::DetectionLevelRing& ring() { return expandedView().detectionLevelRing(); }

    static constexpr int blockSize = 512;
};

} // namespace host
