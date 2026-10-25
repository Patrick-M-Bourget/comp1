// The Expanded View's Gain Reduction meters (the Gate's, the compressor's and the Limiter's), the Detection Level ring on
// Threshold and the Ceiling ring, which read the editor's level history; and which controls are available in
// Zero Latency, and at each sample rate's Oversampling cap.

#include "Footer.h"
#include "Measure.h"
#include "Playing.h"
#include "comp1/Latency.h"
#include "plugkit/ui/staple/Tokens.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

using host::Playing;
using host::silence;
using host::sine;
using host::steady;
using host::checkFallsAt20DbPerSecond;

} // namespace

TEST_CASE ("The Gain Reduction meter reads 0 with no compression, and repaints only while it moves")
{
    Playing host;
    host.playFor (0.2, silence());
    CHECK (host.meter().shownDb() == 0.0);
    // Below Threshold: still nothing to show, and nothing repainted.
    auto& repaints = harness::countRepaintsOf (host.meter());
    host.playFor (0.3, sine (0.01));
    CHECK (host.meter().shownDb() == 0.0);
    CHECK (repaints.count == 0);
    host.meter().setCachedComponentImage (nullptr);
}

TEST_CASE ("The Gain Reduction meter's rail is the design system's meter track as the Output Meter's is")
{
    Playing host;
    host.playFor (0.2, silence());
    auto& meter = host.meter();
    REQUIRE (meter.shownDb() == 0.0);
    // Drawn alone, over nothing: the rail runs down the meter's right half, and its middle is its own colour, with no
    // bar over it.
    const auto snapshot = meter.createComponentSnapshot (meter.getLocalBounds(), false);
    const auto rail = snapshot.getPixelAt (meter.getWidth() * 3 / 4, meter.getHeight() / 2);
    const auto track = staple::tokens::colour::meterTrack;
    CHECK (static_cast<int> (rail.getRed()) == static_cast<int> (track.getRed()));
    CHECK (static_cast<int> (rail.getGreen()) == static_cast<int> (track.getGreen()));
    CHECK (static_cast<int> (rail.getBlue()) == static_cast<int> (track.getBlue()));
    CHECK (std::abs (static_cast<int> (rail.getAlpha()) - static_cast<int> (track.getAlpha())) <= 1);
}

TEST_CASE ("On a steady level above Threshold the Gain Reduction meter reads the Transfer Curve's Gain Reduction, and falls at 20 dB/s when the tone stops")
{
    Playing host;
    // -30 dB at 4:1 with a hard knee and a quick Release, so the compressor gives its gain back at once and the meter's
    // fall is its own.
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 0.0f);
    host.edit (parameters::releaseId, 5.0f);
    // A steady -6 dB reads -6 dB: 24 dB over Threshold, of which 4:1 takes off 18.
    auto& repaints = harness::countRepaintsOf (host.meter());
    host.playFor (0.5, steady (-6.0));
    // Read just after a tick that read records, as it rises at once: a busy machine's tick may fall a little first.
    CHECK (host.keepPlayingUntil (steady (-6.0), [&] { return std::abs (host.meter().shownDb() - 18.0) <= 0.5; }));
    CHECK (repaints.count > 0);
    host.meter().setCachedComponentImage (nullptr);

    // Read as a meter, titled and valued, and no stop for Tab.
    auto* handler = host.meter().getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::progressBar);
    CHECK (handler->getTitle() == "Gain Reduction");
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK (handler->getValueInterface()->getCurrentValueAsString().endsWith (" dB"));
    CHECK_FALSE (host.meter().getWantsKeyboardFocus());

    checkFallsAt20DbPerSecond (host, [&] { return host.meter().shownDb(); }, 0.0);
}

TEST_CASE ("With the Gate closed on a quiet passage the Gate Gain Reduction meter reads Gate Range, and only it; opened, it falls at 20 dB/s")
{
    Playing host;
    host.edit (parameters::gateId, 1.0f);
    host.edit (parameters::gateThresholdId, -20.0f);
    host.edit (parameters::gateRangeId, 20.0f);
    auto& gate = host.gateMeter();
    auto& repaints = harness::countRepaintsOf (gate);
    // A steady -50 dB, 30 dB under Gate Threshold, closes the Gate by all of Gate Range. It is under the compressor's
    // Threshold, and the Limiter is off: only the Gate acts.
    host.playFor (0.5, steady (-50.0));
    CHECK (host.keepPlayingUntil (steady (-50.0), [&] { return std::abs (gate.shownDb() - 20.0) <= 0.5; }));
    CHECK (repaints.count > 0);
    gate.setCachedComponentImage (nullptr);
    CHECK (host.meter().shownDb() == 0.0);
    CHECK (host.limiterMeter().shownDb() == 0.0);

    // Read as a meter, titled and valued; no stop for Tab, and no taker of the mouse.
    auto* handler = gate.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::progressBar);
    CHECK (handler->getTitle() == "Gate Gain Reduction");
    CHECK (gate.getName() == "Gate Gain Reduction");
    CHECK (gate.getTooltip() == "Gate Gain Reduction");
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK (host.keepPlayingUntil (steady (-50.0), [&] { return handler->getValueInterface()->getCurrentValueAsString() == "20.0 dB"; }));
    CHECK_FALSE (gate.getWantsKeyboardFocus());
    bool clicks = true, childClicks = true;
    gate.getInterceptsMouseClicks (clicks, childClicks);
    CHECK_FALSE (clicks);
    CHECK_FALSE (childClicks);

    // A steady -6 dB opens the Gate: its meter falls as the compressor's does, though the tone plays on.
    checkFallsAt20DbPerSecond (host, [&] { return gate.shownDb(); }, 0.0, steady (-6.0));
}

TEST_CASE ("In Zero Latency the Limiter Gain Reduction meter reads how far Output Gain takes a steady level over Ceiling, and only it")
{
    Playing host;
    host.edit (parameters::processingModeId, 0.0f);
    // The compressor at 1:1, so that only the Limiter acts.
    host.edit (parameters::ratioId, 1.0f);
    host.edit (parameters::limiterId, 1.0f);
    host.edit (parameters::ceilingId, 0.0f);
    auto& limiter = host.limiterMeter();
    // A steady -6 dB with Output Gain +6 dB reaches Ceiling: about nothing to take off.
    host.edit (parameters::outputGainId, 6.0f);
    host.playFor (0.5, steady (-6.0));
    CHECK (limiter.shownDb() <= 0.5);
    // With +12 dB it is 6 dB over.
    host.edit (parameters::outputGainId, 12.0f);
    CHECK (host.keepPlayingUntil (steady (-6.0), [&] { return std::abs (limiter.shownDb() - 6.0) <= 0.5; }));
    CHECK (host.meter().shownDb() == 0.0);
    CHECK (host.gateMeter().shownDb() == 0.0);

    auto* handler = limiter.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::progressBar);
    CHECK (handler->getTitle() == "Limiter Gain Reduction");
    CHECK (limiter.getName() == "Limiter Gain Reduction");
    CHECK (limiter.getTooltip() == "Limiter Gain Reduction");
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK (handler->getValueInterface()->getCurrentValueAsString().endsWith (" dB"));
    CHECK_FALSE (limiter.getWantsKeyboardFocus());
    bool clicks = true, childClicks = true;
    limiter.getInterceptsMouseClicks (clicks, childClicks);
    CHECK_FALSE (clicks);
    CHECK_FALSE (childClicks);
}

TEST_CASE ("With only the compressor acting, the Gate's and the Limiter's Gain Reduction meters read 0")
{
    Playing host;
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 0.0f);
    host.playFor (0.5, steady (-6.0));
    CHECK (host.keepPlayingUntil (steady (-6.0), [&] { return std::abs (host.meter().shownDb() - 18.0) <= 0.5; }));
    CHECK (host.gateMeter().shownDb() == 0.0);
    CHECK (host.limiterMeter().shownDb() == 0.0);
}

TEST_CASE ("With the Gate or the Limiter off its Gain Reduction meter reads 0, on a level that would have it act")
{
    Playing host;
    host.edit (parameters::gateThresholdId, -20.0f);
    host.edit (parameters::gateRangeId, 20.0f);
    host.edit (parameters::ratioId, 1.0f);
    host.edit (parameters::ceilingId, 0.0f);
    // Under Gate Threshold.
    host.playFor (0.5, steady (-50.0));
    CHECK (host.gateMeter().shownDb() == 0.0);
    // 6 dB over Ceiling.
    host.edit (parameters::outputGainId, 12.0f);
    host.playFor (0.5, steady (-6.0));
    CHECK (host.limiterMeter().shownDb() == 0.0);
}

TEST_CASE ("A Gain Reduction over the meters' scale fills the bar, and a screen reader reads the true dB")
{
    Playing host;
    host.edit (parameters::gateId, 1.0f);
    host.edit (parameters::gateThresholdId, -20.0f);
    host.edit (parameters::gateRangeId, 30.0f);
    auto& gate = host.gateMeter();
    auto* handler = gate.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    host.playFor (0.5, steady (-60.0));
    CHECK (host.keepPlayingUntil (steady (-60.0), [&] { return handler->getValueInterface()->getCurrentValueAsString() == "30.0 dB"; }));
    CHECK (comp1::GainReductionMeter::scaleDb == 24.0);
    CHECK (comp1::GainReductionMeter::position (gate.shownDb()) == 1.0);
}

TEST_CASE ("With no audio the Gate's and the Limiter's Gain Reduction meters read 0 and ticks repaint neither")
{
    Playing host;
    host.playFor (0.2, silence());
    auto& gate = host.gateMeter();
    auto& limiter = host.limiterMeter();
    auto& gateRepaints = harness::countRepaintsOf (gate);
    auto& limiterRepaints = harness::countRepaintsOf (limiter);
    host.playFor (0.3, silence());
    CHECK (gate.shownDb() == 0.0);
    CHECK (limiter.shownDb() == 0.0);
    CHECK (gateRepaints.count == 0);
    CHECK (limiterRepaints.count == 0);
    gate.setCachedComponentImage (nullptr);
    limiter.setCachedComponentImage (nullptr);
}

TEST_CASE ("The Detection Level ring fills Threshold's lane to the Detection Level on Threshold's scale, and falls at 20 dB/s after")
{
    Playing host;
    CHECK (host.ring().shownDb() == -60.0);
    host.playFor (0.3, sine (std::pow (10.0, -12.0 / 20.0)));
    CHECK (host.keepPlayingUntil (sine (std::pow (10.0, -12.0 / 20.0)), [&] { return std::abs (host.ring().shownDb() + 12.0) <= 0.5; }));
    checkFallsAt20DbPerSecond (host, [&] { return host.ring().shownDb(); }, -60.0);

    // Display only: a press on the lane leaves Threshold where it was, and its tooltip lines are empty.
    auto& knob = host.knob ("Threshold");
    const float threshold = host.value (parameters::thresholdId);
    const auto onLane = knob.getFaceCentre().translated (0.0f, -(knob.getFaceRadius() + staple::tokens::knob::ringOffset));
    REQUIRE (knob.isOnRing (onLane));
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    knob.mouseDown (harness::mouseEvent (knob, onLane, mods));
    knob.mouseDrag (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), mods, onLane));
    knob.mouseUp (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), {}, onLane));
    CHECK (host.value (parameters::thresholdId) == threshold);
    CHECK (host.ring().ringTitle().isEmpty());
    CHECK (host.ring().ringValue().isEmpty());
}

TEST_CASE ("The Ceiling ring, in the output popover, fills Ceiling's lane down from its top by the Limiter's Gain Reduction, on Ceiling's scale, and falls at 20 dB/s after")
{
    Playing host;
    host.openOutputPopover();
    CHECK (host.ceilingRing().shownDb() == 0.0);
    // A steady 0 dB level at 1:1, so the compressor leaves it alone, into the Limiter at -6 dBFS: 6 dB of Gain Reduction.
    host.edit (parameters::ratioId, 1.0f);
    host.edit (parameters::limiterId, 1.0f);
    host.edit (parameters::ceilingId, -6.0f);
    host.playFor (0.3, steady (0.0));
    CHECK (host.keepPlayingUntil (steady (0.0), [&] { return std::abs (host.ceilingRing().shownDb() - 6.0) <= 0.5; }));
    // Filled from Ceiling's top, 0 dBFS, down to -6 dBFS: the top fifth of its -30 to 0 dBFS sweep.
    CHECK_THAT (host.ceilingRing().filledFrom(), WithinAbs (0.8, 0.02));
    checkFallsAt20DbPerSecond (host, [&] { return host.ceilingRing().shownDb(); }, 0.0);

    // Display only: a press on the lane leaves Ceiling where it was, and its tooltip lines are empty.
    auto& knob = host.knob ("Ceiling");
    const float ceiling = host.value (parameters::ceilingId);
    const auto onLane = knob.getFaceCentre().translated (0.0f, -(knob.getFaceRadius() + staple::tokens::knob::ringOffset));
    REQUIRE (knob.isOnRing (onLane));
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    knob.mouseDown (harness::mouseEvent (knob, onLane, mods));
    knob.mouseDrag (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), mods, onLane));
    knob.mouseUp (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), {}, onLane));
    CHECK (host.value (parameters::ceilingId) == ceiling);
    CHECK (host.ceilingRing().ringTitle().isEmpty());
    CHECK (host.ceilingRing().ringValue().isEmpty());
}

TEST_CASE ("With the Limiter off the Ceiling ring stays empty and repaints nothing, on a level that would have it act")
{
    Playing host;
    host.openOutputPopover();
    host.edit (parameters::ratioId, 1.0f);
    host.edit (parameters::ceilingId, -6.0f);
    auto& repaints = harness::countRepaintsOf (host.knob ("Ceiling"));
    host.playFor (0.5, steady (0.0));
    CHECK (host.ceilingRing().shownDb() == 0.0);
    CHECK (repaints.count == 0);
    host.knob ("Ceiling").setCachedComponentImage (nullptr);
}

TEST_CASE ("With Detection Source External and no Sidechain, the Detection Level ring stays at the floor")
{
    Playing host;
    host.edit (parameters::detectionSourceId, 1.0f);
    host.playFor (0.3, sine (0.5));
    CHECK (host.ring().shownDb() == -60.0);
}

TEST_CASE ("In Zero Latency Lookahead and Oversampling are disabled, their values kept; in High Quality they are enabled")
{
    host::EditorHost host;
    host.edit (parameters::lookaheadId, 2.0f);
    auto& lookahead = host.knob ("Lookahead");
    auto& oversampling = host.showing<comp1::ChoiceChip> ("Oversampling");
    const auto enabled = [&] (bool on) { return lookahead.isEnabled() == on && oversampling.isEnabled() == on; };
    REQUIRE (host.settleUntil ([&] { return enabled (false); }));
    host.edit (parameters::processingModeId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return enabled (true); }));
    host.edit (parameters::processingModeId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return enabled (false); }));
    CHECK_THAT (host.value (parameters::lookaheadId), WithinAbs (2.0f, 1.0e-4));
}

TEST_CASE ("Oversampling offers the factors up to each sample rate's cap and shows the one in force; before a sample rate, the assumed one's")
{
    host::EditorHost host;
    host.edit (parameters::processingModeId, 1.0f);
    auto& chip = host.showing<comp1::ChoiceChip> ("Oversampling");
    const auto offered = [&chip] {
        juce::StringArray items;
        const auto menu = chip.menu();
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            items.add (it.getItem().text);
        return items;
    };
    const juce::StringArray all { "1x", "2x", "4x", "8x" };
    // Not prepared yet: 48 kHz assumed.
    CHECK (offered() == all);

    const double sampleRate = GENERATE (from_range (comp1::test::sampleRates()));
    CAPTURE (sampleRate);
    host.processor.prepareToPlay (sampleRate, 512);
    // 1x to 8x at 44.1 and 48 kHz, to 4x at 88.2 and 96 kHz, to 2x at 176.4 and 192 kHz.
    const int factors = sampleRate <= 48000.0 ? 4 : sampleRate <= 96000.0 ? 3 : 2;
    juce::StringArray expected;
    for (int i = 0; i < factors; ++i)
        expected.add (all[i]);
    CHECK (offered() == expected);

    // 8x stored: the cap shown, and ticked.
    host.edit (parameters::oversamplingId, 3.0f);
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == expected[factors - 1]; }));
    const auto menu = chip.menu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        CHECK (it.getItem().isTicked == (it.getItem().text == expected[factors - 1]));
    // Picking one writes it.
    auto action = harness::actionOf (menu, "1x");
    REQUIRE (action != nullptr);
    action();
    CHECK (host.value (parameters::oversamplingId) == 0.0f);
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == "1x"; }));
}
