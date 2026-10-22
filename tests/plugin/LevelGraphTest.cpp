// The Expanded View's Level and Gain Reduction Graph, the Level tab: what it takes from the editor's level history and
// what it draws, read through its test model (LevelGraph::drawnAt) rather than its pixels.

#include "LevelGraph.h"
#include "Measure.h"
#include "Playing.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

constexpr int run = 16;

// A subscriber that keeps every record handed to it.
struct Recorder final : comp1::LevelHistory::Subscriber
{
    std::vector<comp1::LevelRecord> records;
    void levelsRead (std::span<const comp1::LevelRecord> read) override { records.insert (records.end(), read.begin(), read.end()); }
};

struct Graphing : host::Playing
{
    using Playing::Playing;

    comp1::LevelGraph& graph() { return expandedView().display().levelGraph(); }

    // Plays samples of signal, from its sample from on, a few blocks at a time, waiting after each until the graph has
    // taken every run played: none is dropped by a tick the machine was too busy to run.
    template <typename Signal>
    void feed (int samples, Signal&& signal, int from = 0)
    {
        const int chunk = blockSize * std::max (4, static_cast<int> (std::lround (sampleRate() / 12000.0)));
        for (int done = 0; done < samples; done += chunk)
        {
            const int now = std::min (chunk, samples - done);
            const auto expected = graph().recordsTaken() + static_cast<std::uint64_t> (now / run);
            for (int block = 0; block < now; block += blockSize)
                play (1, std::min (blockSize, now - block), [&signal, at = from + done + block] (int i) { return signal (at + i); });
            REQUIRE (settleUntil ([&] { return graph().recordsTaken() >= expected; }));
            REQUIRE (graph().recordsTaken() == expected);
        }
    }

    template <typename Signal>
    void feedFor (double seconds, Signal&& signal)
    {
        feed (samplesIn (seconds), std::forward<Signal> (signal));
    }

    // Whole runs, so a feed takes exactly as many records as it plays.
    int samplesIn (double seconds) const { return static_cast<int> (std::lround (seconds * sampleRate() / run)) * run; }
};

} // namespace

TEST_CASE ("The Level and Gain Reduction Graph takes every record played, in order, alongside the other subscribers")
{
    Graphing host;
    Recorder recorder;
    host.expandedView().levelHistory().subscribe (recorder);
    // A run of 16 samples at each level, rising run by run: the kth at (k + 1) / 512 of full scale.
    constexpr int runs = 8 * host::Playing::blockSize / run;
    const auto levelOf = [] (int k) { return (k + 1) / 512.0; };
    const auto before = host.graph().recordsTaken();
    host.feed (runs * run, [&] (int n) { return levelOf (n / run); });

    CHECK (host.graph().recordsTaken() - before == static_cast<std::uint64_t> (runs));
    REQUIRE (recorder.records.size() == static_cast<size_t> (runs));
    for (int k = 0; k < runs; ++k)
    {
        CAPTURE (k);
        const auto taken = host.graph().recordAgo (runs - 1 - k);
        CHECK_THAT (taken.inputDb, WithinAbs (20.0 * std::log10 (levelOf (k)), 1.0e-3));
        CHECK_THAT (taken.inputDb, WithinAbs (recorder.records[static_cast<size_t> (k)].inputDb, 1.0e-4));
        CHECK_THAT (taken.outputDb, WithinAbs (recorder.records[static_cast<size_t> (k)].outputDb, 1.0e-4));
        CHECK_THAT (taken.gainReductionDb, WithinAbs (recorder.records[static_cast<size_t> (k)].gainReductionDb, 1.0e-4));
    }
    host.expandedView().levelHistory().unsubscribe (recorder);
}

TEST_CASE ("The Level and Gain Reduction Graph reads a steady sine's Input and Output Level, and silence at the bottom of its scale")
{
    Graphing host;
    // Nothing compressed, and nothing added: Threshold at 0 dB, Auto Gain off, Output Gain 0 dB.
    host.edit (parameters::thresholdId, 0.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::outputGainId, 0.0f);
    host.feedFor (0.5, host::sine (std::pow (10.0, -12.0 / 20.0), host.sampleRate()));
    for (const double ago : { 0.0, 0.1, 0.3 })
    {
        CAPTURE (ago);
        const auto drawn = host.graph().drawnAt (ago);
        CHECK_THAT (drawn.inputDb, WithinAbs (-12.0, 0.1));
        CHECK_THAT (drawn.outputDb, WithinAbs (-12.0, 0.1));
        CHECK (drawn.gainReductionDb == 0.0);
    }

    host.feedFor (0.5, host::silence());
    const auto drawn = host.graph().drawnAt (0.1);
    CHECK (drawn.inputDb == -comp1::LevelGraph::scaleDb);
    CHECK (drawn.outputDb == -comp1::LevelGraph::scaleDb);
    CHECK (drawn.gainReductionDb == 0.0);
    // And where it holds nothing yet, as silence.
    CHECK (host.graph().drawnAt (4.9).inputDb == -comp1::LevelGraph::scaleDb);
}

namespace
{

constexpr double markerDb = -6.0;

// A marker: two runs at markerDb.
auto marker() { return host::steady (markerDb); }
constexpr int markerSamples = 2 * run;

// Whether the graph shows the marker, at its Input Level, secondsAgo: in that column or the next either side.
bool showsMarkerAt (Graphing& host, double secondsAgo)
{
    const double column = host.graph().columnSeconds();
    for (const double ago : { secondsAgo - column, secondsAgo, secondsAgo + column })
        if (std::abs (host.graph().drawnAt (std::max (0.0, ago)).inputDb - markerDb) < 0.1)
            return true;
    return false;
}

// Plays silence, the marker, then seconds of silence: the marker is then seconds ago.
void playMarker (Graphing& host, double seconds)
{
    host.feed (markerSamples, marker());
    host.feedFor (seconds, host::silence());
}

// Checks the marker shows secondsAgo, and not a few columns either side.
void checkMarkerAt (Graphing& host, double secondsAgo)
{
    CAPTURE (secondsAgo, host.graph().getWidth());
    CHECK (showsMarkerAt (host, secondsAgo));
    const double column = host.graph().columnSeconds();
    CHECK_FALSE (showsMarkerAt (host, secondsAgo - 3 * column));
    if (secondsAgo + 3 * column < comp1::LevelGraph::spanSeconds)
        CHECK_FALSE (showsMarkerAt (host, secondsAgo + 3 * column));
}

} // namespace

TEST_CASE ("The Level and Gain Reduction Graph shows the newest record at its right edge, and a record t seconds ago t seconds from it, over a 5 s span at any sample rate")
{
    const auto& rates = comp1::test::sampleRates();
    const double sampleRate = GENERATE_REF (rates.front(), rates.back());
    CAPTURE (sampleRate);
    Graphing host (sampleRate);
    REQUIRE (juce::exactlyEqual (host.processor.getSampleRate(), sampleRate));

    // At the right edge: the newest record, as taken.
    host.edit (parameters::thresholdId, -30.0f);
    host.feedFor (0.3, host::steady (-10.0));
    const auto newest = host.graph().recordAgo (0);
    const auto drawn = host.graph().drawnAt (0.0);
    CHECK_THAT (drawn.inputDb, WithinAbs (newest.inputDb, 0.01));
    CHECK_THAT (drawn.outputDb, WithinAbs (newest.outputDb, 0.01));
    CHECK_THAT (drawn.gainReductionDb, WithinAbs (newest.gainReductionDb, 0.01));
    CHECK (drawn.gainReductionDb > 1.0);

    // A marker moving left as the audio after it plays, a column at a time, until it leaves at 5 s.
    host.feedFor (0.5, host::silence());
    playMarker (host, 0.5);
    double ago = 0.5;
    for (const double more : { 1.0, 1.5, 1.75 })
    {
        checkMarkerAt (host, ago);
        host.feedFor (more, host::silence());
        ago += more;
    }
    checkMarkerAt (host, ago);
    REQUIRE (ago < comp1::LevelGraph::spanSeconds);
    host.feedFor (comp1::LevelGraph::spanSeconds - ago + 0.1, host::silence());
    CHECK_FALSE (showsMarkerAt (host, comp1::LevelGraph::spanSeconds - host.graph().columnSeconds() / 2.0));
}

TEST_CASE ("A one-run transient in quiet audio shows in its pixel column at the graph's minimum and maximum widths")
{
    Graphing host;
    const auto& window = comp1::PluginProcessor::expandedWindow;
    const auto size = GENERATE_COPY (window.minimum, window.maximum);
    CAPTURE (size.x, size.y);
    host.editor->setSize (size.x, size.y);
    REQUIRE (host.graph().getWidth() > 0);

    host.feedFor (1.0, host::steady (-50.0));
    // One run, on the run grid, at markerDb.
    host.feed (run, marker());
    const int after = host.samplesIn (2.0);
    host.feed (after, host::steady (-50.0));
    const double ago = after / host.sampleRate();
    CHECK (std::abs (host.graph().drawnAt (ago).inputDb - markerDb) < 0.1);
    // No column but its own shows it.
    int columnsShowing = 0;
    for (int column = 0; column < host.graph().getWidth(); ++column)
        if (host.graph().drawnAt ((column + 0.5) * host.graph().columnSeconds()).inputDb > -20.0)
            ++columnsShowing;
    CHECK (columnsShowing == 1);
}

TEST_CASE ("With no audio the Level and Gain Reduction Graph holds still and asks for no repaint; playing again resumes it from where it stopped")
{
    Graphing host;
    host.feedFor (0.5, host::silence());
    playMarker (host, 1.0);
    const auto taken = host.graph().recordsTaken();
    auto& repaints = harness::countRepaintsOf (host.graph());
    // Ticks, with nothing played.
    host.settle (300);
    CHECK (repaints.count == 0);
    CHECK (host.graph().recordsTaken() == taken);
    checkMarkerAt (host, 1.0);

    host.feedFor (0.5, host::silence());
    CHECK (repaints.count > 0);
    checkMarkerAt (host, 1.5);
    host.graph().setCachedComponentImage (nullptr);
}

TEST_CASE ("Records played while the Detection EQ tab is shown are in the Level and Gain Reduction Graph when Level is chosen again")
{
    Graphing host;
    auto& display = host.expandedView().display();
    auto& detectionEq = host.showing<juce::Button> ("Detection EQ");
    REQUIRE (detectionEq.onClick != nullptr);
    detectionEq.onClick();
    REQUIRE_FALSE (host.graph().isShowing());

    playMarker (host, 1.0);

    auto& level = host.showing<juce::Button> ("Level");
    REQUIRE (level.onClick != nullptr);
    level.onClick();
    REQUIRE (display.shownTab() == comp1::DisplayTab::level);
    REQUIRE (host.graph().isShowing());
    checkMarkerAt (host, 1.0);
}

TEST_CASE ("Resizing the window and changing the UI Scale keep the Level and Gain Reduction Graph's history and its 5 s span")
{
    Graphing host;
    playMarker (host, 3.0);
    const auto taken = host.graph().recordsTaken();
    const auto& window = comp1::PluginProcessor::expandedWindow;
    auto& scale = host.showing<plugkit::ui::UiScaleMenu> ("UI Scale");
    for (const int percent : { 100, 200, 75 })
        for (const auto size : { window.minimum, window.maximum, window.defaultSize })
        {
            CAPTURE (percent, size.x, size.y);
            scale.pick (percent);
            host.editor->setSize (juce::roundToInt (size.x * percent / 100.0), juce::roundToInt (size.y * percent / 100.0));
            CHECK (host.graph().recordsTaken() == taken);
            // The span is 5 s whatever the width.
            CHECK_THAT (host.graph().columnSeconds() * host.graph().getWidth(), WithinAbs (comp1::LevelGraph::spanSeconds, 1.0e-9));
            checkMarkerAt (host, 3.0);
        }
}

namespace
{

// Clean at -30 dB, 4:1, a hard knee, a 10 ms Attack and a 100 ms Release, nothing held: 20 dB over Threshold the
// Transfer Curve takes off 15 dB.
void compress (Graphing& host)
{
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 0.0f);
    host.edit (parameters::attackId, 10.0f);
    host.edit (parameters::releaseId, 100.0f);
    host.edit (parameters::autoReleaseId, 0.0f);
    host.edit (parameters::holdId, 0.0f);
}

constexpr double loudDb = -10.0, quietDb = -60.0, transferCurveGainReductionDb = 15.0;
constexpr double attackSeconds = 0.01, releaseSeconds = 0.1;

} // namespace

TEST_CASE ("The Level and Gain Reduction Graph shows Attack, as Gain Reduction rising to the Transfer Curve's after a step up, and Release, as it falling back after the step down")
{
    Graphing host;
    compress (host);
    host.feedFor (0.5, host::steady (quietDb));
    host.feedFor (0.5, host::steady (loudDb));
    host.feedFor (1.0, host::steady (quietDb));
    // Seconds ago: the step up and the step down.
    const double up = 1.5, down = 1.0;
    const auto gainReductionAt = [&] (double ago) { return host.graph().drawnAt (ago).gainReductionDb; };

    // Attack: none before the step, then rising, not at once, to most of the Transfer Curve's within a few Attack
    // times, and to all of it.
    CHECK (gainReductionAt (up + 0.05) < 0.01);
    CHECK (gainReductionAt (up - 0.4 * attackSeconds) < 0.75 * transferCurveGainReductionDb);
    CHECK (gainReductionAt (up - 4.0 * attackSeconds) > 0.9 * transferCurveGainReductionDb);
    CHECK_THAT (gainReductionAt (down + 0.1), WithinAbs (transferCurveGainReductionDb, 0.5));
    // The levels the Gain Reduction followed.
    CHECK_THAT (host.graph().drawnAt (down + 0.1).inputDb, WithinAbs (loudDb, 0.1));
    CHECK_THAT (host.graph().drawnAt (down + 0.1).outputDb, WithinAbs (loudDb - transferCurveGainReductionDb, 0.6));

    // Release: still most of it just after the step down, about a 1/e of it a Release time on, and next to none after
    // several.
    CHECK (gainReductionAt (down - 0.2 * releaseSeconds) > 0.6 * transferCurveGainReductionDb);
    const double oneRelease = gainReductionAt (down - releaseSeconds);
    CHECK (oneRelease > 0.2 * transferCurveGainReductionDb);
    CHECK (oneRelease < 0.55 * transferCurveGainReductionDb);
    CHECK (gainReductionAt (down - 6.0 * releaseSeconds) < 0.5);
}

TEST_CASE ("The Level and Gain Reduction Graph shows pumping: on a burst train with a period between Attack and Release, Gain Reduction rises and falls once a period")
{
    Graphing host;
    compress (host);
    // Loud for the first half of each 50 ms period, quiet for the rest.
    constexpr double periodSeconds = 0.05;
    const int period = static_cast<int> (std::lround (periodSeconds * host.sampleRate()));
    const auto loud = host::steady (loudDb), quiet = host::steady (quietDb);
    host.feedFor (2.0, [&] (int n) { return n % period < period / 2 ? loud (n) : quiet (n); });

    // The peaks over the last second, across every column: a rise of over 1 dB from a trough, then a fall of over 1 dB.
    const double column = host.graph().columnSeconds();
    constexpr double swingDb = 1.0, stretchSeconds = 1.0;
    int peaks = 0;
    bool rising = false;
    double trough = 1.0e9, peak = -1.0e9;
    for (double ago = stretchSeconds; ago >= 0.0; ago -= column)
    {
        const double reduction = host.graph().drawnAt (ago).gainReductionDb;
        if (! rising && reduction > trough + swingDb)
        {
            rising = true;
            peak = reduction;
        }
        else if (rising && reduction < peak - swingDb)
        {
            rising = false;
            ++peaks;
            trough = reduction;
        }
        trough = std::min (trough, reduction);
        peak = std::max (peak, reduction);
    }
    CAPTURE (period, column);
    const int periods = static_cast<int> (std::lround (stretchSeconds / periodSeconds));
    CHECK (std::abs (peaks - periods) <= 1);
}

TEST_CASE ("A screen reader finds the Level and Gain Reduction Graph as a read-only image read with the latest levels; it is no stop for Tab")
{
    Graphing host;
    auto* handler = host.graph().getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::image);
    CHECK (handler->getTitle() == "Level and Gain Reduction Graph");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK_FALSE (host.graph().getWantsKeyboardFocus());
    bool clicks = true, childClicks = true;
    host.graph().getInterceptsMouseClicks (clicks, childClicks);
    CHECK_FALSE (clicks);
    CHECK_FALSE (childClicks);

    // Silence, then a steady level 20 dB over Threshold, with nothing added after the compressor.
    host.feedFor (0.1, host::silence());
    CHECK (handler->getValueInterface()->getCurrentValueAsString()
           == juce::String (juce::CharPointer_UTF8 ("Input Level \xe2\x88\x92inf dBFS, Output Level \xe2\x88\x92inf dBFS, Gain Reduction 0.0 dB")));
    compress (host);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::outputGainId, 0.0f);
    host.feedFor (0.5, host::steady (loudDb));
    CHECK (handler->getValueInterface()->getCurrentValueAsString()
           == juce::String (juce::CharPointer_UTF8 ("Input Level \xe2\x88\x92" "10.0 dBFS, Output Level \xe2\x88\x92" "25.0 dBFS, Gain Reduction 15.0 dB")));
}
