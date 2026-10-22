#include "EditorHost.h"
#include "LevelHistory.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{

// A subscriber that keeps every record handed to it.
struct Recorder final : comp1::LevelHistory::Subscriber
{
    std::vector<comp1::LevelRecord> records;
    void levelsRead (std::span<const comp1::LevelRecord> read) override { records.insert (records.end(), read.begin(), read.end()); }
};

constexpr int run = 16, blockSize = 512;

} // namespace

TEST_CASE ("The editor's level history hands every record played after it opened to every subscriber, in order")
{
    host::EditorHost host;
    host.editor.reset();
    harness::useLayout (host.processor, juce::AudioChannelSet::stereo());
    // Played while no editor is open: old by the time one opens.
    host.play (8, blockSize, [] (int) { return 0.9; });

    host.reopen();
    auto& history = host.expandedView().levelHistory();
    Recorder first, second;
    history.subscribe (first);
    history.subscribe (second);
    // A run of 16 samples at each level, rising run by run: the kth at (k + 1) / 256 of full scale.
    constexpr int blocks = 8, runs = blocks * blockSize / run;
    const auto levelOf = [] (int k) { return (k + 1) / 256.0; };
    host.play (blocks, blockSize, [&] (int n) { return levelOf (n / run); });

    REQUIRE (host.settleUntil ([&] { return first.records.size() >= runs && second.records.size() >= runs; }));
    host.settle (200);
    REQUIRE (first.records.size() == static_cast<size_t> (runs));
    REQUIRE (second.records.size() == static_cast<size_t> (runs));
    for (int k = 0; k < runs; ++k)
    {
        CAPTURE (k);
        const double expected = 20.0 * std::log10 (levelOf (k));
        CHECK_THAT (first.records[static_cast<size_t> (k)].inputDb, WithinAbs (expected, 1.0e-3));
        CHECK (second.records[static_cast<size_t> (k)].inputDb == first.records[static_cast<size_t> (k)].inputDb);
    }

    // Unsubscribed, it is handed nothing more.
    history.unsubscribe (second);
    host.play (2, blockSize, [] (int) { return 0.5; });
    REQUIRE (host.settleUntil ([&] { return first.records.size() == static_cast<size_t> (runs + 2 * blockSize / run); }));
    CHECK (second.records.size() == static_cast<size_t> (runs));
    history.unsubscribe (first);
}
