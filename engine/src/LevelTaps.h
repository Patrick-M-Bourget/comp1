#pragma once

// The level taps comp1 adds to plugkit's Output Level: a run's levels (RunLevels), gathered on the audio thread, and
// the level history (LevelHistory), which carries a record of each run to one reader thread.

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
// Output Level over every channel, the most Gain Reduction, and the loudest Detection Level. take() makes the run's
// record and starts the next run afresh.
class RunLevels
{
public:
    // A piece of one channel's input, as process() receives it, non-finite samples silenced.
    void noteInput (const float* samples, int count) { inputPeak = std::max (inputPeak, peakOf (samples, count)); }

    // A piece of one channel's output, after everything.
    void noteOutput (const float* samples, int count) { outputPeak = std::max (outputPeak, peakOf (samples, count)); }

    LevelRecord take()
    {
        const LevelRecord record { .inputDb = levelDb (inputPeak), .outputDb = levelDb (outputPeak) };
        inputPeak = outputPeak = 0.0f;
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

    // In dB, at least the floor; the floor too for NaN.
    static double levelDb (float peak)
    {
        const double db = plugkit::gainToDecibels (static_cast<double> (peak));
        return db > plugkit::levelFloorDb ? db : plugkit::levelFloorDb;
    }

    float inputPeak = 0.0f, outputPeak = 0.0f;
};

// The level history: a single-producer single-consumer ring of LevelRecords. The audio thread pushes a record per
// completed run, one reader pops; when the reader falls behind, new records are dropped, so what it reads stays in
// order with no gap inside what it gets.
//
// The capacity is a power of two covering at least minimumSeconds of runs at the prepared sample rate: the time
// between two editor frames, with margin. The reader may go on reading while the host prepares the Engine again, so
// a ring is never freed or moved while the Engine lives: a prepare that needs more replaces it with a bigger one and
// keeps the old one, and a prepare that needs less keeps the ring it has.
class LevelHistory
{
public:
    static constexpr double minimumSeconds = 0.15;

    // The smallest power of two of records covering minimumSeconds at sampleRate.
    static int capacityFor (double sampleRate)
    {
        const auto needed = static_cast<unsigned> (std::max (1.0, std::ceil (minimumSeconds * sampleRate / plugkit::runSamples)));
        return static_cast<int> (std::bit_ceil (needed));
    }

    // Allocates when the sample rate needs a bigger ring than the one held; otherwise leaves the ring, and what it
    // holds, alone.
    void prepare (double sampleRate)
    {
        const auto needed = static_cast<std::size_t> (capacityFor (sampleRate));
        if (auto* ring = current.load (std::memory_order_relaxed); ring != nullptr && ring->records.size() >= needed)
            return;
        rings.push_back (std::make_unique<Ring> (needed));
        current.store (rings.back().get(), std::memory_order_release);
    }

    // How many records the ring holds: the most one pop can return.
    int capacity() const
    {
        const auto* ring = current.load (std::memory_order_acquire);
        return ring == nullptr ? 0 : static_cast<int> (ring->records.size());
    }

    // From the audio thread. Dropped when the ring is full.
    void push (const LevelRecord& record)
    {
        auto* ring = current.load (std::memory_order_acquire);
        if (ring == nullptr)
            return;
        const auto write = ring->writeIndex.load (std::memory_order_relaxed);
        const auto read = ring->readIndex.load (std::memory_order_acquire);
        if (write - read == ring->records.size())
            return;
        ring->records[write & ring->mask] = record;
        ring->writeIndex.store (write + 1, std::memory_order_release);
    }

    // From the reader thread: copies up to maxRecords, oldest first, and returns how many.
    int pop (LevelRecord* destination, int maxRecords)
    {
        auto* ring = current.load (std::memory_order_acquire);
        if (ring == nullptr)
            return 0;
        const auto read = ring->readIndex.load (std::memory_order_relaxed);
        const auto write = ring->writeIndex.load (std::memory_order_acquire);
        const auto n = std::min (static_cast<std::size_t> (std::max (maxRecords, 0)), write - read);
        for (std::size_t i = 0; i < n; ++i)
            destination[i] = ring->records[(read + i) & ring->mask];
        ring->readIndex.store (read + n, std::memory_order_release);
        return static_cast<int> (n);
    }

    // From the reader thread: drops what the ring holds, so the next pop returns only records pushed after this. It
    // moves the read index up to the write index, which the producer only ever sees grow, as after a pop.
    void skip()
    {
        auto* ring = current.load (std::memory_order_acquire);
        if (ring == nullptr)
            return;
        ring->readIndex.store (ring->writeIndex.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    // A ring's indices live with its records, so a reader finishing a pop on an outgrown ring touches nothing of the
    // new one.
    struct Ring
    {
        explicit Ring (std::size_t capacity) : records (capacity), mask (capacity - 1) {}
        std::vector<LevelRecord> records;
        std::size_t mask;
        std::atomic<std::size_t> writeIndex { 0 }, readIndex { 0 };
    };

    std::vector<std::unique_ptr<Ring>> rings; // every ring made since construction, the current one last
    std::atomic<Ring*> current { nullptr };
};

} // namespace comp1
