// The editor's CPU per tick with the Spectrum tab shown, against the Level tab: a benchmark, hidden from the normal
// run (ctest never lists it) and run by scripts/check.sh paint (docs/performance.md, "Editor CPU"). It prints its
// figures and fails on none of them.

#include "Measure.h"
#include "Playing.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <utility>
#include <vector>

namespace parameters = comp1::parameters;
using comp1::DisplayTab;

namespace
{

constexpr double tickSeconds = 1.0 / 60.0;
constexpr int rounds = 5;
constexpr int ticksPerRound = 120;
// What a Retina display draws the editor at.
constexpr float paintScale = 2.0f;

// Per-tick costs in seconds, one per tick timed.
struct Costs
{
    std::vector<double> tick, paint, total;
};

double mean (const std::vector<double>& xs)
{
    double sum = 0.0;
    for (const double x : xs)
        sum += x;
    return sum / static_cast<double> (xs.size());
}

double p95 (std::vector<double> xs)
{
    const auto at = xs.begin() + static_cast<std::ptrdiff_t> (0.95 * static_cast<double> (xs.size() - 1));
    std::nth_element (xs.begin(), at, xs.end());
    return *at;
}

void print (const char* tab, const char* what, const std::vector<double>& xs)
{
    std::printf ("  %-9s %-13s mean %7.1f us   p95 %7.1f us\n", tab, what, 1.0e6 * mean (xs), 1.0e6 * p95 (xs));
}

// The editor playing steady pink noise at -18 dB RMS on a stereo track at 48 kHz, compressed by Clean at Threshold
// -30 dB, so every display has something to show: the meters, the Level and Gain Reduction Graph and both spectra.
struct EditorCpuHost : host::Playing
{
    comp1::test::Channels noise = comp1::test::pinkNoise (1, 2 * 48000, -18.0, 1);

    EditorCpuHost()
    {
        REQUIRE (processor.chooseStyle (comp1::Style::Clean));
        edit (parameters::thresholdId, -30.0f);
        fixTickSeconds (tickSeconds);
    }

    double sample (int n) const
    {
        const auto& channel = noise.front();
        return channel[static_cast<std::size_t> (n) % channel.size()];
    }

    void show (DisplayTab tab)
    {
        processor.setDisplayTab (tab);
        auto& display = expandedView().display();
        REQUIRE (settleUntil ([&] { return display.shownTab() == tab && display.tabContent (tab).isShowing(); }));
    }

    // Plays one tick's worth of audio (1/60 s, in two blocks), then waits for the editor's next tick, timing the
    // thread's CPU across the wait, and paints the display as a window would after the tick. Only the work is timed:
    // the wait sleeps between JUCE's timers, which the thread's CPU time doesn't count. The paint goes to a native image,
    // drawn by the renderer the platform's windows draw with (CoreGraphics on macOS), not JUCE's software renderer.
    void timeTicks (int ticks, Costs* costs)
    {
        constexpr int samplesPerBlock = 400;
        auto& display = expandedView().display();
        for (int t = 0; t < ticks; ++t)
        {
            play (2, samplesPerBlock, [this] (int i) { return sample (played + i); });
            played += 2 * samplesPerBlock;
            const int before = editorTick().count();
            const double start = comp1::test::threadSeconds();
            REQUIRE (settleUntil ([&] { return editorTick().count() > before; }));
            const double tick = (comp1::test::threadSeconds() - start) / (editorTick().count() - before);
            double paint = 0.0;
            harness::paintImage (static_cast<int> (paintScale * static_cast<float> (display.getWidth())),
                                 static_cast<int> (paintScale * static_cast<float> (display.getHeight())),
                                 [&] (juce::Graphics& g) {
                                     g.addTransform (juce::AffineTransform::scale (paintScale));
                                     const double paintStart = comp1::test::threadSeconds();
                                     display.paintEntireComponent (g, false);
                                     paint = comp1::test::threadSeconds() - paintStart;
                                 },
                                 juce::NativeImageType());
            if (costs != nullptr)
            {
                costs->tick.push_back (tick);
                costs->paint.push_back (paint);
                costs->total.push_back (tick + paint);
            }
        }
    }

    // A round on tab: shown, a second of ticks untimed so its displays have settled, then ticksPerRound timed.
    void round (DisplayTab tab, Costs& costs)
    {
        show (tab);
        timeTicks (60, nullptr);
        timeTicks (ticksPerRound, &costs);
    }

    int played = 0;
};

} // namespace

TEST_CASE ("Editor CPU: the editor's tick and the display's paint per tick, on the Spectrum tab and on the Level tab", "[.editor-cpu][paint]")
{
    EditorCpuHost host;
    Costs level, spectrum;
    // The tabs take turns, so a change in the machine's load over the run falls on both.
    for (int r = 0; r < rounds; ++r)
    {
        host.round (DisplayTab::level, level);
        host.round (DisplayTab::spectrum, spectrum);
    }
    REQUIRE (level.total.size() == static_cast<std::size_t> (rounds * ticksPerRound));
    REQUIRE (spectrum.total.size() == level.total.size());

    std::printf ("Editor CPU per tick, %d ticks a tab, %.0f Hz ticks fixed at 1/60 s, display painted at %.0fx:\n",
                 rounds * ticksPerRound, 1.0 / tickSeconds, static_cast<double> (paintScale));
    for (auto [name, costs] : { std::pair { "Level", &level }, std::pair { "Spectrum", &spectrum } })
    {
        print (name, "tick", costs->tick);
        print (name, "paint", costs->paint);
        print (name, "tick + paint", costs->total);
    }
    std::printf ("  Spectrum / Level, tick + paint: mean %.2fx   p95 %.2fx\n", mean (spectrum.total) / mean (level.total),
                 p95 (spectrum.total) / p95 (level.total));
}
