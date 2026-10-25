// The editor's CPU per tick with the Spectrum tab shown, against the Level tab: a benchmark, hidden from the normal
// run (ctest never lists it) and run by scripts/check.sh paint (docs/performance.md, "Editor CPU"). It prints its
// figures and fails on none of them.

#include "Measure.h"
#include "Playing.h"
#include "TimingResponseInset.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
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

TEST_CASE ("Editor CPU: a frame of a drag on the Timing Response inset's handles, the Expanded View at its default size", "[.editor-cpu][.drag-frame]")
{
    EditorCpuHost host;
    host.show (DisplayTab::level);
    auto& editor = *host.editor;
    const auto size = comp1::PluginProcessor::expandedWindow.defaultSize;
    REQUIRE (editor.getWidth() == size.x);
    REQUIRE (editor.getHeight() == size.y);
    auto& inset = host.expandedView().display().timingResponseInset();
    REQUIRE (host.settleUntil ([&] { return inset.drawn().has_value() && ! inset.isWaiting(); }));
    host.timeTicks (60, nullptr);

    constexpr int framesPerHandle = 300;
    // The thread's CPU painting component whole at 2x into a native image, as a window draws it.
    const auto paintTime = [] (juce::Component& component) {
        double painted = 0.0;
        harness::paintImage (static_cast<int> (paintScale * static_cast<float> (component.getWidth())),
                             static_cast<int> (paintScale * static_cast<float> (component.getHeight())),
                             [&] (juce::Graphics& g) {
                                 g.addTransform (juce::AffineTransform::scale (paintScale));
                                 const double paintStart = comp1::test::threadSeconds();
                                 component.paintEntireComponent (g, false);
                                 painted = comp1::test::threadSeconds() - paintStart;
                             },
                             juce::NativeImageType());
        return painted;
    };
    // Per frame: the drag handler's CPU (0 with no drag), then 1/60 s of audio and the tick, then the paints.
    struct Frames
    {
        std::vector<double> drag, tick, insetPaint, editorPaint, frame, wholeFrame;
    };
    int behind = 0, mostBehind = 0; // ticks in a row the curve drawn trailed the settings held
    const auto runFrame = [&] (Frames& frames, const std::function<void()>& move) {
        const double start = comp1::test::threadSeconds();
        if (move)
            move();
        frames.drag.push_back (comp1::test::threadSeconds() - start);
        host.play (2, 400, [&host] (int i) { return host.sample (host.played + i); });
        host.played += 800;
        const int before = host.editorTick().count();
        const double tickStart = comp1::test::threadSeconds();
        REQUIRE (host.settleUntil ([&] { return host.editorTick().count() > before; }));
        frames.tick.push_back ((comp1::test::threadSeconds() - tickStart) / (host.editorTick().count() - before));
        behind = inset.isWaiting() ? behind + 1 : 0;
        mostBehind = std::max (mostBehind, behind);
        frames.insetPaint.push_back (paintTime (inset));
        frames.editorPaint.push_back (paintTime (editor));
        frames.frame.push_back (frames.drag.back() + frames.tick.back() + frames.insetPaint.back());
        frames.wholeFrame.push_back (frames.drag.back() + frames.tick.back() + frames.editorPaint.back());
    };

    // The same frames with no drag, the signal playing: what the editor costs anyway.
    Frames still, dragged;
    for (int f = 0; f < 2 * framesPerHandle; ++f)
        runFrame (still, {});
    for (const auto which : { comp1::TimingHandle::attack, comp1::TimingHandle::release })
    {
        auto& handle = inset.handle (which);
        const auto from = inset.handleCentre (which) - handle.getPosition().toFloat();
        const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        handle.mouseDown (harness::mouseEvent (handle, from, held));
        for (int f = 0; f < framesPerHandle; ++f)
        {
            // A sweep across the axis and back, a few px a frame, as a hand drags.
            const float x = 60.0f * std::sin (2.0f * juce::MathConstants<float>::pi * static_cast<float> (f) / 150.0f);
            runFrame (dragged, [&] { handle.mouseDrag (harness::mouseEvent (handle, from + juce::Point<float> { x, 0.0f }, held, from)); });
        }
        handle.mouseUp (harness::mouseEvent (handle, from, {}, from));
    }
    const auto worst = [] (const std::vector<double>& xs) { return *std::max_element (xs.begin(), xs.end()); };
    std::printf ("Timing Response inset drag, %zu frames, the editor at %d x %d, 1/60 s ticks, painted at %.0fx, message thread CPU:\n",
                 dragged.frame.size(), size.x, size.y, static_cast<double> (paintScale));
    for (auto [name, frames] : { std::pair { "still", &still }, std::pair { "drag", &dragged } })
    {
        print (name, "handler", frames->drag);
        print (name, "tick", frames->tick);
        print (name, "inset paint", frames->insetPaint);
        print (name, "editor paint", frames->editorPaint);
        print (name, "frame", frames->frame);
        print (name, "whole frame", frames->wholeFrame);
    }
    std::printf ("  frame: handler + tick + the inset's paint, what a drag dirties; whole frame: with the whole editor painted, an upper bound\n");
    std::printf ("  worst drag frame %.2f ms, worst whole frame %.2f ms still and %.2f ms dragging (a 60 Hz frame is 16.67 ms)\n",
                 1.0e3 * worst (dragged.frame), 1.0e3 * worst (still.wholeFrame), 1.0e3 * worst (dragged.wholeFrame));
    std::printf ("  the curve drawn trails the handle by at most %d ticks\n", mostBehind);
}
