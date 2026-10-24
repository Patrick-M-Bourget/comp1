#include "EditorHost.h"
#include "Playing.h"
#include "comp1/Style.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{

// The editor as drawn, one pixel per pixel of its window, into the snapshot folder (check.sh screens).
void snapshot (host::EditorHost& host, const juce::String& name)
{
    host.settleAnimations();
    harness::writeSnapshot (*host.editor, name, {}, 1.0f);
}

void openOn (host::EditorHost& host, const juce::AudioChannelSet& channels)
{
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
}

// A 1 kHz sine at 48 kHz, at amplitude.
auto sine (double amplitude)
{
    return [amplitude] (int n) { return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / 48000.0); };
}

} // namespace

TEST_CASE ("Expanded View snapshots: default at 100% and 200%, on mono, with Comp Follow on and off, in High Quality, with signal playing, a lit Clip Light, and the Preset browser open", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    snapshot (host, "expanded-default-100");

    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (200);
    snapshot (host, "expanded-default-200");
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (100);

    openOn (host, juce::AudioChannelSet::mono());
    snapshot (host, "expanded-mono");

    openOn (host, juce::AudioChannelSet::stereo());
    // Comp Follow on (Opto, Comp at 80%), Threshold and Ratio showing the Comp Curve's and Auto Gain on; then off,
    // as the toggle turns it off.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (comp1::parameters::compId, 80.0f);
    snapshot (host, "expanded-comp-follow-on");
    REQUIRE (host.processor.stopFollowingComp());
    snapshot (host, "expanded-comp-follow-off");
    host.processor.session().editHistory().undo();
    host.processor.session().editHistory().undo();
    host.processor.session().editHistory().undo();

    // High Quality: Lookahead and Oversampling enabled.
    host.edit (comp1::parameters::processingModeId, 1.0f);
    snapshot (host, "expanded-high-quality");
    host.processor.session().editHistory().undo();

    // Playing: the Output Meter's bars, the Gain Reduction meter and the Detection Level ring up, as the editor's tick
    // reads them between blocks.
    for (int i = 0; i < 12; ++i)
    {
        host.play (4, 512, sine (0.5));
        host.settle (20);
    }
    harness::writeSnapshot (*host.editor, "expanded-playing", {}, 1.0f);

    // An over, well above whatever Gain Reduction the sine left.
    host.play (1, 512, [] (int n) { return n == 100 ? 8.0 : 0.0; });
    REQUIRE (host.settleUntil ([&] { return host.processor.isClipLit (0); }));
    snapshot (host, "expanded-clip-light");
    host.processor.clearClipLights();

    auto& presets = host.showing<juce::Button> ("Presets");
    REQUIRE (presets.onClick != nullptr);
    presets.onClick();
    snapshot (host, "expanded-preset-browser");
}

TEST_CASE ("Expanded View snapshots: the Level tab's Level and Gain Reduction Graph with a compressed tone stepping up and down, then a burst train, and under Global Bypass", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    // Clean at -30 dB, 4:1, a hard knee, a 10 ms Attack and a 100 ms Release: Attack and Release show at the tone's
    // steps, and pumping on a burst train of a 50 ms period.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (comp1::parameters::compFollowId, 0.0f);
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::ratioId, 4.0f);
    host.edit (comp1::parameters::kneeId, 0.0f);
    host.edit (comp1::parameters::attackId, 10.0f);
    host.edit (comp1::parameters::releaseId, 100.0f);
    const auto loud = sine (std::pow (10.0, -10.0 / 20.0)), quiet = sine (std::pow (10.0, -50.0 / 20.0));
    constexpr int period = 2400; // 50 ms at 48 kHz
    const auto burst = [&] (int n) { return n % period < period / 2 ? loud (n) : quiet (n); };
    // Seconds of signal, a few blocks between the editor's ticks.
    const auto playFor = [&host] (double seconds, auto&& signal) {
        for (int n = 0; n < static_cast<int> (seconds * 48000.0); n += 4 * 512)
        {
            host.play (4, 512, [&signal, n] (int i) { return signal (n + i); });
            host.settle (20);
        }
    };
    playFor (0.4, quiet);
    playFor (1.6, loud);
    playFor (0.6, quiet);
    playFor (2.4, burst);
    REQUIRE (host.expandedView().display().shownTab() == comp1::DisplayTab::level);
    harness::writeSnapshot (*host.editor, "expanded-level-graph", {}, 1.0f);

    // With no more audio the graph holds still while it and the Transfer Curve inset fade into Global Bypass.
    auto& display = host.expandedView().display();
    host.edit (comp1::parameters::globalBypassId, 1.0f);
    REQUIRE (host.settleUntil ([&] {
        return juce::exactlyEqual (display.levelGraph().globalBypassFade(), 1.0f) && juce::exactlyEqual (display.transferCurveInset().graph().globalBypassFade(), 1.0f)
               && juce::exactlyEqual (display.transferCurveInset().lookOf (comp1::InsetHandle::ratio).alpha, comp1::GlobalBypassFade::scale);
    }));
    harness::writeSnapshot (*host.editor, "expanded-level-graph-global-bypass", {}, 1.0f);
}

TEST_CASE ("Expanded View snapshots: the Transfer Curve tab with a hard and a wide Knee, and with signal playing so the Detection Level's dot shows, with Global Bypass off and on", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    // Clean at -30 dB and 4:1, its Comp Follow off, so the stored settings draw.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (comp1::parameters::compFollowId, 0.0f);
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::ratioId, 4.0f);
    host.processor.setDisplayTab (comp1::DisplayTab::transferCurve);
    auto& graph = host.expandedView().display().transferCurveGraph();
    REQUIRE (host.settleUntil ([&] { return graph.isShowing(); }));

    host.edit (comp1::parameters::kneeId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (graph.curve().kneeDb, 0.0); }));
    snapshot (host, "expanded-transfer-curve-hard-knee");

    host.edit (comp1::parameters::kneeId, 24.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (graph.curve().kneeDb, 24.0); }));
    snapshot (host, "expanded-transfer-curve-wide-knee");

    // A -12 dB sine, the editor's tick reading between blocks, until the dot is up.
    const auto tone = sine (std::pow (10.0, -12.0 / 20.0));
    REQUIRE (host.settleUntil ([&] {
        host.play (4, 512, tone);
        return graph.dotDb().has_value() && *graph.dotDb() > -14.0;
    }));
    harness::writeSnapshot (*host.editor, "expanded-transfer-curve-playing", {}, 1.0f);

    // Still playing, so the dot stays up while the graph fades into Global Bypass.
    host.edit (comp1::parameters::globalBypassId, 1.0f);
    REQUIRE (host.settleUntil ([&] {
        host.play (4, 512, tone);
        return juce::exactlyEqual (graph.globalBypassFade(), 1.0f) && graph.dotDb().has_value() && *graph.dotDb() > -14.0;
    }));
    harness::writeSnapshot (*host.editor, "expanded-transfer-curve-playing-global-bypass", {}, 1.0f);
}

TEST_CASE ("Expanded View snapshot: the Gate's, the compressor's and the Limiter's Gain Reduction meters all reading", "[.screens]")
{
    host::Playing host;
    // A loud passage the compressor takes 18 dB off (-30 dB at 4:1, a hard knee), which Output Gain then takes 12 dB
    // over Ceiling for the Limiter; then a quiet one under Gate Threshold, which the Gate closes on by all of Gate Range,
    // while the other two meters fall.
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::ratioId, 4.0f);
    host.edit (comp1::parameters::kneeId, 0.0f);
    host.edit (comp1::parameters::outputGainId, 36.0f);
    host.edit (comp1::parameters::limiterId, 1.0f);
    host.edit (comp1::parameters::ceilingId, 0.0f);
    host.edit (comp1::parameters::gateId, 1.0f);
    host.edit (comp1::parameters::gateThresholdId, -40.0f);
    host.edit (comp1::parameters::gateRangeId, 20.0f);
    host.edit (comp1::parameters::gateReleaseId, 20.0f);
    host.playFor (1.0, host::steady (-6.0));
    REQUIRE (host.keepPlayingUntil (host::steady (-60.0), [&] { return host.gateMeter().shownDb() >= 19.5; }));
    CHECK (host.meter().shownDb() > 6.0);
    CHECK (host.limiterMeter().shownDb() > 3.0);
    harness::writeSnapshot (*host.editor, "expanded-gain-reduction-meters", {}, 1.0f);
}

TEST_CASE ("Expanded View snapshot: the output popover open, its Ceiling ring filled with the Limiter's Gain Reduction", "[.screens]")
{
    host::Playing host;
    host.openOutputPopover();
    // A steady 0 dB level at 1:1 into the Limiter at -12 dBFS: 12 dB of Gain Reduction, the top two fifths of
    // Ceiling's lane.
    host.edit (comp1::parameters::ratioId, 1.0f);
    host.edit (comp1::parameters::limiterId, 1.0f);
    host.edit (comp1::parameters::ceilingId, -12.0f);
    host.playFor (0.5, host::steady (0.0));
    REQUIRE (host.keepPlayingUntil (host::steady (0.0), [&] { return std::abs (host.ceilingRing().shownDb() - 12.0) <= 0.5; }));
    harness::writeSnapshot (*host.editor, "expanded-ceiling-ring", {}, 1.0f);
}

TEST_CASE ("Compact View snapshots: default at 100% and 200%, with Comp Follow on and off, and with the Preset browser open", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::compact);
    snapshot (host, "compact-default-100");

    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (200);
    snapshot (host, "compact-default-200");
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (100);

    // Comp Follow on (Opto, Comp at 80%), then off as grabbing Threshold in the Expanded View turns it off: Comp stays.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (comp1::parameters::compId, 80.0f);
    snapshot (host, "compact-comp-follow-on");
    REQUIRE (host.processor.stopFollowingComp());
    snapshot (host, "compact-comp-follow-off");
    for (int i = 0; i < 3; ++i)
        host.processor.session().editHistory().undo();

    auto& presets = host.showing<juce::Button> ("Presets");
    REQUIRE (presets.onClick != nullptr);
    presets.onClick();
    snapshot (host, "compact-preset-browser");
}

TEST_CASE ("Output popover snapshots: open in the Expanded View and the Compact View, with Auto Gain on, with Comp Follow on, and with Dry mixed in", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    // Auto Gain on at Threshold -30 dB, Output Gain +1 dB and the Limiter on at -1 dBFS.
    host.edit (comp1::parameters::compFollowId, 0.0f);
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::autoGainId, 1.0f);
    host.edit (comp1::parameters::outputGainId, 1.0f);
    host.edit (comp1::parameters::limiterId, 1.0f);
    host.edit (comp1::parameters::ceilingId, -1.0f);
    host.openOutputPopover();
    snapshot (host, "expanded-output-popover");

    // Comp Follow on (Opto, Comp at 80%), Auto Gain on as choosing the Style writes it.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (comp1::parameters::compId, 80.0f);
    snapshot (host, "expanded-output-popover-comp-follow");

    host.outputPopover().close();
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::compact);
    host.openOutputPopover();
    snapshot (host, "compact-output-popover-comp-follow");
    host.edit (comp1::parameters::compFollowId, 0.0f);
    snapshot (host, "compact-output-popover");

    // Parallel compression: Dry at -12 dB, Wet at -2 dB, the readout's mark showing.
    host.edit (comp1::parameters::dryId, -12.0f);
    host.edit (comp1::parameters::wetId, -2.0f);
    snapshot (host, "compact-output-popover-parallel");
    host.outputPopover().close();
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::expanded);
    host.openOutputPopover();
    snapshot (host, "expanded-output-popover-parallel");
}

TEST_CASE ("Footer snapshots: the Output readout with Auto Gain off, then on with its dot, in the Expanded View and close up", "[.screens]")
{
    host::EditorHost host;
    openOn (host, juce::AudioChannelSet::stereo());
    // Threshold -30 dB with Output Gain +1 dB: the readout reads +1.0 dB, then +8.9 dB with Auto Gain's estimate.
    host.edit (comp1::parameters::compFollowId, 0.0f);
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::outputGainId, 1.0f);
    for (const bool on : { false, true })
    {
        host.edit (comp1::parameters::autoGainId, on ? 1.0f : 0.0f);
        REQUIRE (host.settleUntil ([&] { return host.footer().autoGainDot().isVisible() == on; }));
        const juce::String name = on ? "auto-gain-on" : "auto-gain-off";
        snapshot (host, "expanded-" + name);
        // Close up, at 3x: the footer's right end, from just left of the readout, over the window's background.
        auto& footer = host.footer();
        const auto right = host.editor->getLocalArea (&footer, footer.getLocalBounds().withLeft (host.outputReadout().getX() - 24));
        harness::writeSnapshot (*host.editor, "footer-" + name, right, 3.0f);
    }
}

TEST_CASE ("Compact View snapshot: with signal playing, so the Gain Reduction meter reads", "[.screens]")
{
    host::Playing host;
    // -6 dB steady, 24 dB over a -30 dB Threshold at 4:1 with a hard knee: 18 dB of Gain Reduction.
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::ratioId, 4.0f);
    host.edit (comp1::parameters::kneeId, 0.0f);
    host::EditorHost::click (host.expandedView().viewSwitch());
    auto& meter = host.expandedView().compactView().gainReductionMeter();
    host.playFor (0.5, host::steady (-6.0));
    REQUIRE (host.keepPlayingUntil (host::steady (-6.0), [&] { return meter.shownDb() > 17.0; }));
    harness::writeSnapshot (*host.editor, "compact-playing", {}, 1.0f);
}

TEST_CASE ("Expanded View snapshots: the Transfer Curve inset's handles with signal playing, the Ratio handle hovered and the Threshold handle dragged with its readout card, then under Global Bypass", "[.screens]")
{
    host::Playing host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (comp1::parameters::compFollowId, 0.0f);
    host.edit (comp1::parameters::thresholdId, -30.0f);
    host.edit (comp1::parameters::ratioId, 4.0f);
    host.edit (comp1::parameters::kneeId, 6.0f);
    REQUIRE (host.expandedView().display().shownTab() == comp1::DisplayTab::level);
    host.playFor (1.0, sine (0.5));
    auto& inset = host.expandedView().display().transferCurveInset();
    harness::writeSnapshot (*host.editor, "expanded-inset-handles-playing", {}, 1.0f);

    // The pointer's events through the handles' own handlers: a test window isn't in front.
    auto& ratio = inset.handle (comp1::InsetHandle::ratio);
    const auto ratioCentre = inset.handleCentre (comp1::InsetHandle::ratio) - ratio.getPosition().toFloat();
    ratio.mouseEnter (harness::mouseEvent (ratio, ratioCentre));
    auto& threshold = inset.handle (comp1::InsetHandle::threshold);
    const auto thresholdCentre = inset.handleCentre (comp1::InsetHandle::threshold) - threshold.getPosition().toFloat();
    const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    threshold.mouseDown (harness::mouseEvent (threshold, thresholdCentre, held));
    threshold.mouseDrag (harness::mouseEvent (threshold, thresholdCentre + juce::Point<float> { 12.0f, 0.0f }, held, thresholdCentre));
    host.playFor (0.3, sine (0.5));
    harness::writeSnapshot (inset, "inset-handles-hovered-and-dragged", {}, 2.0f);
    threshold.mouseUp (harness::mouseEvent (threshold, thresholdCentre + juce::Point<float> { 12.0f, 0.0f }, {}, thresholdCentre));
    ratio.mouseExit (harness::mouseEvent (ratio, ratioCentre + juce::Point<float> { 30.0f, 0.0f }));

    // Under Global Bypass, still playing so the dot stays up: the inset's curve, dot and handles dimmed.
    host.edit (comp1::parameters::globalBypassId, 1.0f);
    REQUIRE (host.keepPlayingUntil (sine (0.5), [&] {
        return juce::exactlyEqual (inset.graph().globalBypassFade(), 1.0f)
               && juce::exactlyEqual (inset.lookOf (comp1::InsetHandle::ratio).alpha, comp1::GlobalBypassFade::scale);
    }));
    harness::writeSnapshot (inset, "inset-handles-global-bypass", {}, 2.0f);
}
