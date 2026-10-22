#pragma once

// The taps comp1 adds to plugkit's Output Level: a run's levels (RunLevels), gathered on the audio thread; the level
// history (LevelHistory), which carries a record of each run to one reader thread; and the detection source tap
// (DetectionSourceTap), which carries the detection source's samples to one, both on one ring (TapRing).

#include "comp1/Engine.h"

#include <plugkit/OutputLevel.h>
#include <plugkit/RunGrid.h>
#include <plugkit/Units.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace comp1
{

// The levels of the run in progress, gathered on the audio thread as the run is played: the peak Input Level and
// Output Level over every channel, the compressor's, the Gate's and the Limiter's most Gain Reduction, and the loudest
// Detection Level. The Gate and the Limiter note their gain, and take() works out each one's Gain Reduction once, from
// the run's smallest gain. take() makes the run's record and starts the next run afresh.
class RunLevels
{
public:
    // A piece of one channel's input, as process() receives it, non-finite samples silenced.
    void noteInput (const float* samples, int count) { inputPeak = std::max (inputPeak, peakOf (samples, count)); }

    // A piece of one channel's output, after everything.
    void noteOutput (const float* samples, int count) { outputPeak = std::max (outputPeak, peakOf (samples, count)); }

    // One sample of one channel at the detector: the Detection Level it compared with Threshold, and the Gain
    // Reduction the compressor applied for it, 0 or more.
    void noteDetection (double detectionLevelDb, double gainReductionDb)
    {
        loudestDetectionDb = std::max (loudestDetectionDb, detectionLevelDb);
        mostGainReductionDb = std::max (mostGainReductionDb, gainReductionDb);
    }

    // One sample's gain from the Gate, which puts one gain on every channel: 1 for none.
    void noteGateGain (double gain) { smallestGateGain = std::min (smallestGateGain, gain); }

    // One sample's gain from the Limiter, which puts one gain on every channel, after its on/off crossfade and before
    // Zero Latency's safety clip: 1 for none.
    void noteLimiterGain (double gain) { smallestLimiterGain = std::min (smallestLimiterGain, gain); }

    LevelRecord take()
    {
        const LevelRecord record { .inputDb = levelDb (inputPeak),
                                   .outputDb = levelDb (outputPeak),
                                   .gainReductionDb = mostGainReductionDb,
                                   .detectionLevelDb = atLeastFloor (loudestDetectionDb),
                                   .gateGainReductionDb = reductionOf (smallestGateGain),
                                   .limiterGainReductionDb = reductionOf (smallestLimiterGain) };
        *this = {};
        return record;
    }

private:
    static float peakOf (const float* samples, int count)
    {
        float peak = 0.0f;
        for (int i = 0; i < count; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        return peak;
    }

    // At least the floor; the floor too for NaN.
    static double atLeastFloor (double db) { return db > plugkit::levelFloorDb ? db : plugkit::levelFloorDb; }

    static double levelDb (float peak) { return atLeastFloor (plugkit::gainToDecibels (static_cast<double> (peak))); }

    // A run's smallest gain as its most Gain Reduction in dB: exactly 0 for a gain of 1.
    static double reductionOf (double gain) { return gain < 1.0 ? -plugkit::gainToDecibels (gain) : 0.0; }

    float inputPeak = 0.0f, outputPeak = 0.0f;
    double loudestDetectionDb = plugkit::levelFloorDb, mostGainReductionDb = 0.0;
    double smallestGateGain = 1.0, smallestLimiterGain = 1.0;
};

// A tap's ring: a single-producer single-consumer ring of Items, each covering samplesPerItem samples of the audio.
// The audio thread pushes, one reader pops; when the reader falls behind, new items are dropped, so what it reads
// stays in order with no gap inside what it gets.
//
// The capacity is a power of two covering at least minimumSeconds of items at the prepared sample rate: the time
// between two editor frames, with margin. The reader may go on reading while the host prepares the Engine again, so
// a ring is never freed or moved while the Engine lives: a prepare that needs more replaces it with a bigger one and
// keeps the old one, and a prepare that needs less keeps the ring it has.
template <typename Item, int samplesPerItem>
class TapRing
{
public:
    static constexpr double minimumSeconds = 0.15;

    // The smallest power of two of items covering minimumSeconds at sampleRate.
    static int capacityFor (double sampleRate)
    {
        const auto needed = static_cast<unsigned> (std::max (1.0, std::ceil (minimumSeconds * sampleRate / samplesPerItem)));
        return static_cast<int> (std::bit_ceil (needed));
    }

    // Allocates when the sample rate needs a bigger ring than the one held; otherwise leaves the ring, and what it
    // holds, alone.
    void prepare (double sampleRate)
    {
        const auto needed = static_cast<std::size_t> (capacityFor (sampleRate));
        if (auto* ring = current.load (std::memory_order_relaxed); ring != nullptr && ring->items.size() >= needed)
            return;
        rings.push_back (std::make_unique<Ring> (needed));
        current.store (rings.back().get(), std::memory_order_release);
    }

    // How many items the ring holds: the most one pop can return.
    int capacity() const
    {
        const auto* ring = current.load (std::memory_order_acquire);
        return ring == nullptr ? 0 : static_cast<int> (ring->items.size());
    }

    // From the audio thread. Dropped when the ring is full.
    void push (const Item& item) { push (&item, 1); }

    // From the audio thread: count items, oldest first, as count pushes of one would push them, those that don't fit
    // dropped.
    void push (const Item* items, int count)
    {
        auto* ring = current.load (std::memory_order_acquire);
        if (ring == nullptr)
            return;
        const auto write = ring->writeIndex.load (std::memory_order_relaxed);
        const auto read = ring->readIndex.load (std::memory_order_acquire);
        const auto n = std::min (static_cast<std::size_t> (std::max (count, 0)), ring->items.size() - (write - read));
        for (std::size_t i = 0; i < n; ++i)
            ring->items[(write + i) & ring->mask] = items[i];
        ring->writeIndex.store (write + n, std::memory_order_release);
    }

    // From the reader thread: copies up to maxItems, oldest first, and returns how many.
    int pop (Item* destination, int maxItems)
    {
        auto* ring = current.load (std::memory_order_acquire);
        if (ring == nullptr)
            return 0;
        const auto read = ring->readIndex.load (std::memory_order_relaxed);
        const auto write = ring->writeIndex.load (std::memory_order_acquire);
        const auto n = std::min (static_cast<std::size_t> (std::max (maxItems, 0)), write - read);
        for (std::size_t i = 0; i < n; ++i)
            destination[i] = ring->items[(read + i) & ring->mask];
        ring->readIndex.store (read + n, std::memory_order_release);
        return static_cast<int> (n);
    }

    // From the reader thread: drops what the ring holds, so the next pop returns only items pushed after this. It
    // moves the read index up to the write index, which the producer only ever sees grow, as after a pop.
    void skip()
    {
        auto* ring = current.load (std::memory_order_acquire);
        if (ring == nullptr)
            return;
        ring->readIndex.store (ring->writeIndex.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    // A ring's indices live with its items, so a reader finishing a pop on an outgrown ring touches nothing of the
    // new one.
    struct Ring
    {
        explicit Ring (std::size_t capacity) : items (capacity), mask (capacity - 1) {}
        std::vector<Item> items;
        std::size_t mask;
        std::atomic<std::size_t> writeIndex { 0 }, readIndex { 0 };
    };

    std::vector<std::unique_ptr<Ring>> rings; // every ring made since construction, the current one last
    std::atomic<Ring*> current { nullptr };
};

// The level history: a record per completed run.
using LevelHistory = TapRing<LevelRecord, plugkit::runSamples>;

// The detection source tap: a sample per input sample.
using DetectionSourceTap = TapRing<float, 1>;

} // namespace comp1
