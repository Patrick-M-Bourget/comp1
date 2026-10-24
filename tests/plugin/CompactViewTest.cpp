// The Compact View's controls at work: a Style pick, Comp and Comp Follow (ADR 0004), the Compact Slots (ADR 0011), the
// output popover's Output Gain into the Limiter, the Gain Reduction meter and the editor-wide keys.

#include "Host.h"
#include "Playing.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

// The Compact View shown, by the footer's switch.
void showCompactView (host::EditorHost& host)
{
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::compact);
    REQUIRE (host.expandedView().compactView().isShowing());
}

// The Compact View's control titled title, showing.
template <typename Control>
Control& compact (host::EditorHost& host, const juce::String& title)
{
    auto& control = host.showing<Control> (title);
    REQUIRE (host.expandedView().compactView().isParentOf (&control));
    return control;
}

staple::Knob& compactKnob (host::EditorHost& host, const juce::String& title) { return compact<staple::Knob> (host, title); }

// The host parameter named name.
juce::RangedAudioParameter& parameterNamed (host::EditorHost& host, const juce::String& name)
{
    for (auto* each : host.processor.getParameters())
        if (auto& ranged = dynamic_cast<juce::RangedAudioParameter&> (*each); ranged.getName (100) == name)
            return ranged;
    FAIL ("no parameter named " << name);
    throw std::logic_error ("unreachable");
}

constexpr std::array everyStyle { comp1::Style::Clean, comp1::Style::VCA,        comp1::Style::FET, comp1::Style::Opto,
                                  comp1::Style::Vocal, comp1::Style::Instrument, comp1::Style::Bus, comp1::Style::Drums };

} // namespace

TEST_CASE ("The Compact View's Comp knob is the kit's hero, a Compact Slot's primary the kit's large and a knob Companion the kit's small, while the Expanded View's Comp stays large")
{
    host::EditorHost host;
    CHECK (host.knob ("Comp").getDiameter() == staple::tokens::knob::gain);
    showCompactView (host);
    CHECK (compactKnob (host, "Comp").getDiameter() == staple::tokens::knob::hero);
    // Clean: Attack over Knee in slot A.
    CHECK (compactKnob (host, "Attack").getDiameter() == staple::tokens::knob::gain);
    CHECK (compactKnob (host, "Knee").getDiameter() == staple::tokens::knob::small);
}

TEST_CASE ("Each Style's Compact Slots show its row, A's primary and Companion then B's, titled with their parameters' names and nothing else; each writes its parameter as one undo step, Comp Follow staying on")
{
    // ADR 0011's table, each slot's primary then its Companion, by the parameters' names.
    struct Row
    {
        comp1::Style style;
        std::array<const char*, 4> names;
    };
    const auto row = GENERATE (values<Row> ({
        { comp1::Style::Clean, { "Attack", "Knee", "Release", "Auto Release" } },
        { comp1::Style::VCA, { "Attack", "Detection High-Pass", "Release", "Auto Release" } },
        { comp1::Style::FET, { "Attack", "Colour", "Release", "Hold" } },
        { comp1::Style::Opto, { "Colour", "Range", "Detection High-Pass Frequency", "Detection High-Pass" } },
        { comp1::Style::Vocal, { "Gate Threshold", "Gate", "Release", "Auto Release" } },
        { comp1::Style::Instrument, { "Attack", "Knee", "Release", "Auto Release" } },
        { comp1::Style::Bus, { "Attack", "Detection High-Pass", "Release", "Auto Release" } },
        { comp1::Style::Drums, { "Attack", "Hold", "Drive", "Saturation" } },
    }));
    CAPTURE (static_cast<int> (row.style));
    host::EditorHost host;
    showCompactView (host);
    REQUIRE (host.processor.chooseStyle (row.style));
    host.settle();
    auto& body = host.expandedView().compactView();

    // In each slot, its primary then its Companion; the body's controls are those and Comp, nothing else.
    const std::array<juce::Component*, 4> shown { &body.compactSlot (0).primary(), &body.compactSlot (0).companion(),
                                                  &body.compactSlot (1).primary(), &body.compactSlot (1).companion() };
    std::set<juce::String> expected { "Comp" };
    for (size_t i = 0; i < shown.size(); ++i)
    {
        CAPTURE (i);
        CHECK (shown[i]->getTitle() == row.names[i]);
        CHECK (&compact<juce::Component> (host, row.names[i]) == shown[i]);
        expected.insert (row.names[i]);
    }
    std::set<juce::String> titles;
    for (auto* c : host.findAll<juce::Component> ([&body] (juce::Component& c) {
             return body.isParentOf (&c) && c.isShowing() && c.getWantsKeyboardFocus() && c.getTitle().isNotEmpty();
         }))
        titles.insert (c->getTitle());
    CHECK (titles == expected);

    // Each edited by mouse: one undo step, only its parameter written, inside its gesture, Comp Follow on throughout.
    REQUIRE (host.value (parameters::compFollowId) == 1.0f);
    auto& history = host.processor.session().editHistory();
    for (auto* control : shown)
    {
        const auto name = control->getTitle();
        CAPTURE (name);
        auto& parameter = parameterNamed (host, name);
        const float before = parameter.getValue();
        const int steps = history.undoSteps();
        host::GestureRecorder recorder;
        host.processor.addListener (&recorder);
        if (auto* knob = dynamic_cast<staple::Knob*> (control))
            // Down from near the top, up otherwise.
            host::EditorHost::drag (*knob, before > 0.5f ? -20.0f : 20.0f);
        else
            host::EditorHost::click (dynamic_cast<juce::Button&> (*control));
        host.processor.removeListener (&recorder);
        CHECK_FALSE (juce::exactlyEqual (parameter.getValue(), before));
        CHECK (history.undoSteps() == steps + 1);
        REQUIRE_FALSE (recorder.changes.empty());
        for (const auto& [index, inGesture] : recorder.changes)
        {
            CHECK (index == parameter.getParameterIndex());
            CHECK (inGesture);
        }
        CHECK (host.value (parameters::compFollowId) == 1.0f);
        history.undo();
        CHECK (juce::exactlyEqual (parameter.getValue(), before));
    }
}

TEST_CASE ("Following a Style change, a Preset load or an A/B switch, the Compact Slots write nothing, and settings no Style writes keep their values while hidden")
{
    host::EditorHost host;
    showCompactView (host);
    // Settings no Style's defaults include, which Opto's slot B and Drums' show.
    host.edit (parameters::detectionHighPassFrequencyId, 150.0f);
    host.edit (parameters::detectionHighPassOnId, 1.0f);
    host.edit (parameters::driveId, 9.0f);
    host.edit (parameters::saturationId, 1.0f);
    host.settle();
    auto& session = host.processor.session();
    const juce::String how = GENERATE (as<juce::String> {}, "chip", "preset", "A/B");
    CAPTURE (how);
    for (const auto style : { comp1::Style::Opto, comp1::Style::Drums, comp1::Style::Vocal, comp1::Style::Clean })
    {
        CAPTURE (static_cast<int> (style));
        if (how == "chip")
            REQUIRE (host.processor.chooseStyle (style));
        else if (how == "preset")
        {
            REQUIRE (host.processor.chooseStyle (style));
            const auto preset = session.presetState();
            REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
            host.settle();
            REQUIRE (session.loadPreset (preset, "Saved"));
        }
        else
        {
            // B holds the Style and A Clean, shown; then B.
            session.selectCompareSide (plugkit::ui::CompareSide::B);
            REQUIRE (host.processor.chooseStyle (style));
            session.selectCompareSide (plugkit::ui::CompareSide::A);
            REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
            host.settle();
            session.selectCompareSide (plugkit::ui::CompareSide::B);
        }
        REQUIRE (host.value (parameters::styleId) == static_cast<float> (style));
        // The tick seeing the Style rebinds the slots, writing nothing.
        host::GestureRecorder recorder;
        host.processor.addListener (&recorder);
        host.settle();
        host.processor.removeListener (&recorder);
        CHECK (recorder.changes.empty());
        CHECK (host.expandedView().compactView().compactSlot (0).primary().getTitle()
               == (style == comp1::Style::Opto ? "Colour" : style == comp1::Style::Vocal ? "Gate Threshold" : "Attack"));
        CHECK_THAT (host.value (parameters::detectionHighPassFrequencyId), WithinAbs (150.0f, 0.01));
        CHECK (host.value (parameters::detectionHighPassOnId) == 1.0f);
        CHECK_THAT (host.value (parameters::driveId), WithinAbs (9.0f, 1.0e-4));
        CHECK (host.value (parameters::saturationId) == 1.0f);
    }
    // Opto's slot B and Drums' show them.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.settle();
    CHECK_THAT (compactKnob (host, "Detection High-Pass Frequency").getValue(), WithinAbs (150.0, 0.01));
    CHECK (compact<juce::ToggleButton> (host, "Detection High-Pass").getToggleState());
    REQUIRE (host.processor.chooseStyle (comp1::Style::Drums));
    host.settle();
    CHECK_THAT (compactKnob (host, "Drive").getValue(), WithinAbs (9.0, 1.0e-4));
    CHECK (compact<juce::ToggleButton> (host, "Saturation").getToggleState());
}

TEST_CASE ("In every Style Tab walks the Compact View's body in one order: slot A's primary, its Companion, Comp, slot B's primary, its Companion")
{
    host::EditorHost host;
    showCompactView (host);
    const auto style = GENERATE (from_range (everyStyle.begin(), everyStyle.end()));
    CAPTURE (static_cast<int> (style));
    REQUIRE (host.processor.chooseStyle (style));
    host.settle();
    auto& body = host.expandedView().compactView();
    const std::vector<juce::Component*> expected { &body.compactSlot (0).primary(), &body.compactSlot (0).companion(), &compactKnob (host, "Comp"),
                                                   &body.compactSlot (1).primary(), &body.compactSlot (1).companion() };
    expected.front()->grabKeyboardFocus();
    REQUIRE (expected.front()->hasKeyboardFocus (false));
    for (size_t i = 1; i < expected.size(); ++i)
    {
        CAPTURE (i);
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        CHECK (juce::Component::getCurrentlyFocusedComponent() == expected[i]);
    }
}

TEST_CASE ("A Style picked from the footer's chip in the Compact View writes that Style's defaults as one undo step, and the Expanded View shows them after switching")
{
    host::EditorHost host;
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::attackId, 30.0f);
    showCompactView (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    auto& chip = host.showing<comp1::ChoiceChip> ("Style");
    REQUIRE (host.footer().isParentOf (&chip));
    auto pick = harness::actionOf (chip.menu(), "Drums (FET)");
    REQUIRE (pick != nullptr);
    pick();
    CHECK (host.value (parameters::styleId) == static_cast<float> (comp1::Style::Drums));
    // Drums' defaults (comp1/Style.h): Comp Follow on and a 5 ms Attack.
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::attackId), WithinAbs (5.0f, 1.0e-4));
    CHECK (history.undoSteps() == steps + 1);

    host::EditorHost::click (host.expandedView().viewSwitch());
    CHECK (host.showing<comp1::ChoiceChip> ("Style").getButtonText() == "Style: Drums");
    REQUIRE (host.settleUntil ([&] { return host.showing<juce::ToggleButton> ("Comp Follow").getToggleState(); }));
    CHECK_THAT (host.knob ("Attack").getValue(), WithinAbs (5.0, 1.0e-4));

    history.undo();
    CHECK (host.value (parameters::compFollowId) == 0.0f);
    CHECK_THAT (host.value (parameters::attackId), WithinAbs (30.0f, 1.0e-4));
}

TEST_CASE ("With Comp Follow off, grabbing the Compact View's Comp turns it on, Comp nearest the stored Threshold on the Comp Curve, then the edit, as one undo step")
{
    host::EditorHost host;
    // Opto at Comp 50%, Comp Follow off and the stored Threshold at the Comp Curve's 80% (CompParameterTest's).
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::thresholdId, -32.4f);
    const float compBefore = host.value (parameters::compId);
    REQUIRE_THAT (compBefore, WithinAbs (50.0f, 1.0e-4));
    showCompactView (host);
    auto& comp = compactKnob (host, "Comp");
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    const juce::String how = GENERATE (as<juce::String> {}, "press", "drag", "typed", "key", "screen reader");
    CAPTURE (how);
    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    if (how == "press")
        host::EditorHost::drag (comp, 0.0f);
    else if (how == "drag")
        host::EditorHost::drag (comp, 20.0f);
    else if (how == "typed")
        REQUIRE (comp.commitTypedText ("60 %"));
    else if (how == "screen reader")
    {
        auto* handler = comp.getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        REQUIRE (handler->getValueInterface() != nullptr);
        handler->getValueInterface()->setValueAsString ("60 %");
    }
    else
    {
        comp.grabKeyboardFocus();
        REQUIRE (comp.hasKeyboardFocus (false));
        CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    }
    host.processor.removeListener (&recorder);

    CHECK (host.value (parameters::compFollowId) == 1.0f);
    if (how == "press")
        CHECK_THAT (host.value (parameters::compId), WithinAbs (80.0f, 0.05));
    else if (how == "drag")
        // 20 px of the knob's 200 px for 0 to 100%, from 80%.
        CHECK_THAT (host.value (parameters::compId), WithinAbs (90.0f, 0.1));
    else if (how == "typed" || how == "screen reader")
        CHECK_THAT (host.value (parameters::compId), WithinAbs (60.0f, 1.0e-4));
    else
    {
        // One step up from 80%, not from the 50% before the grab.
        CHECK (host.value (parameters::compId) > 80.0f);
        CHECK (host.value (parameters::compId) < 82.0f);
    }
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }

    history.undo();
    CHECK (host.value (parameters::compFollowId) == 0.0f);
    CHECK (host.value (parameters::compId) == compBefore);
    CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (-32.4f, 1.0e-4));
}

TEST_CASE ("With Comp Follow on, a drag of the Compact View's Comp moves it as one undo step, Comp Follow staying on, and the Expanded View's Comp shows it")
{
    host::EditorHost host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    REQUIRE (host.value (parameters::compFollowId) == 1.0f);
    showCompactView (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();
    host::EditorHost::drag (compactKnob (host, "Comp"), 20.0f);
    CHECK_THAT (host.value (parameters::compId), WithinAbs (60.0f, 0.1));
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK (history.undoSteps() == steps + 1);

    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.settleUntil ([&] { return std::abs (host.knob ("Comp").getValue() - 60.0) <= 0.1; }));
    history.undo();
    CHECK_THAT (host.value (parameters::compId), WithinAbs (50.0f, 1.0e-4));
}

TEST_CASE ("In the Compact View the output popover's Output Gain drag is one undo step, and with the Limiter on a steady tone it pushes over the Ceiling never passes it")
{
    host::Playing host;
    // A steady -12 dB, at 1:1 so the compressor leaves it alone, into the Limiter at -6 dBFS.
    host.edit (parameters::ratioId, 1.0f);
    host.edit (parameters::limiterId, 1.0f);
    host.edit (parameters::ceilingId, -6.0f);
    showCompactView (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();
    const double ceiling = std::pow (10.0, -6.0 / 20.0);

    const auto tone = host::steady (-12.0);
    double most = 0.0;
    int n = 0;
    const auto playBlocks = [&] (int blocks) {
        const int from = n;
        auto last = host.play (blocks, host::Playing::blockSize, [&tone, from] (int i) { return tone (from + i); }, host::EditorHost::silent, [&most] (const auto& buffer) {
            for (int ch = 0; ch < 2; ++ch)
                most = std::max (most, static_cast<double> (buffer.getMagnitude (ch, 0, buffer.getNumSamples())));
        });
        n += blocks * host::Playing::blockSize;
        return last;
    };
    playBlocks (20);
    // Under the Ceiling so far: -12 dB out.
    CHECK_THAT (most, WithinAbs (std::pow (10.0, -12.0 / 20.0), 1.0e-3));

    // Up by 72 px of 200 for the 72 dB range: about +26 dB, so the tone would reach +14 dBFS, 20 dB over the Ceiling.
    host.openOutputPopover();
    host::EditorHost::drag (host.knob ("Output Gain"), 72.0f);
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE (host.value (parameters::outputGainId) > 20.0f);
    const auto buffer = playBlocks (100);
    CHECK (most <= ceiling * (1.0 + 1.0e-6));
    // Held at the Ceiling, not below it.
    CHECK (buffer.getMagnitude (0, 0, buffer.getNumSamples()) > ceiling * 0.99);
}

TEST_CASE ("On a steady level above Threshold the Compact View's Gain Reduction meter reads the Transfer Curve's Gain Reduction, repainting only while it moves")
{
    host::Playing host;
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 0.0f);
    host.edit (parameters::releaseId, 5.0f);
    showCompactView (host);
    auto& meter = host.expandedView().compactView().gainReductionMeter();
    REQUIRE (meter.isShowing());
    CHECK (meter.getName() == "Gain Reduction");

    // Under Threshold: nothing to show, and nothing repainted.
    host.playFor (0.2, host::silence());
    auto& repaints = harness::countRepaintsOf (meter);
    host.playFor (0.3, host::sine (0.01));
    CHECK (meter.shownDb() == 0.0);
    CHECK (repaints.count == 0);

    // A steady -6 dB, 24 dB over Threshold, of which 4:1 takes off 18.
    host.playFor (0.5, host::steady (-6.0));
    CHECK (host.keepPlayingUntil (host::steady (-6.0), [&] { return std::abs (meter.shownDb() - 18.0) <= 0.5; }));
    CHECK (repaints.count > 0);
    meter.setCachedComponentImage (nullptr);

    auto* handler = meter.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::progressBar);
    CHECK (handler->getTitle() == "Gain Reduction");
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK_FALSE (meter.getWantsKeyboardFocus());
}

TEST_CASE ("In the Compact View Cmd-Z undoes, Shift-Cmd-Z redoes and Cmd-B toggles Global Bypass, with a knob of its own focused")
{
    host::EditorHost host;
    showCompactView (host);
    const auto command = juce::ModifierKeys::commandModifier;
    auto& attack = compactKnob (host, "Attack");
    const float before = host.value (parameters::attackId);
    host::EditorHost::drag (attack, 20.0f);
    const float edited = host.value (parameters::attackId);
    REQUIRE (edited > before);
    attack.grabKeyboardFocus();
    REQUIRE (attack.hasKeyboardFocus (false));

    CHECK (host.press (juce::KeyPress ('z', command, 0)));
    CHECK (host.value (parameters::attackId) == before);
    CHECK (host.press (juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0)));
    CHECK (host.value (parameters::attackId) == edited);
    CHECK (host.press (juce::KeyPress ('b', command, 0)));
    CHECK (host.value (parameters::globalBypassId) == 1.0f);
    CHECK (host.press (juce::KeyPress ('b', command, 0)));
    CHECK (host.value (parameters::globalBypassId) == 0.0f);
}

TEST_CASE ("A Style change crossfades the Compact Slots: the outgoing controls fade out as the incoming ones fade in over the slots' crossfade time, and nothing moves")
{
    host::EditorHost host;
    showCompactView (host);
    host.settleAnimations();
    auto& body = host.expandedView().compactView();
    auto& a = body.compactSlot (0);
    auto& b = body.compactSlot (1);
    // Clean at rest: no crossfade, every control at full alpha.
    for (auto* slot : { &a, &b })
    {
        CHECK (slot->crossfade() == 1.0f);
        CHECK_FALSE (slot->showsOutgoing());
        CHECK (slot->primary().getAlpha() == 1.0f);
    }
    const auto boundsOf = [&] {
        std::vector<juce::Rectangle<int>> all;
        for (auto* slot : { &a, &b })
        {
            all.push_back (slot->getBoundsInParent());
            all.push_back (slot->primary().getBoundsInParent());
            all.push_back (slot->companion().getBoundsInParent());
        }
        return all;
    };

    // From before the Style change, so what's measured is at least the crossfade's own time.
    const double started = juce::Time::getMillisecondCounterHiRes();
    REQUIRE (host.processor.chooseStyle (comp1::Style::FET));
    // The tick that sees FET rebinds the slots at once, under the outgoing Clean controls fading out.
    REQUIRE (host.settleUntil ([&] { return a.primary().getTitle() == "Attack" && b.companion().getTitle() == "Hold"; }));
    for (auto* slot : { &a, &b })
    {
        CHECK (slot->crossfade() < 1.0f);
        CHECK (slot->showsOutgoing());
        CHECK_THAT (slot->primary().getAlpha(), WithinAbs (slot->crossfade(), 1.0e-6));
        CHECK_THAT (slot->companion().getAlpha(), WithinAbs (slot->crossfade(), 1.0e-6));
    }
    const auto during = boundsOf();

    host.settleAnimations();
    const double took = juce::Time::getMillisecondCounterHiRes() - started;
    CHECK (took >= comp1::CompactSlot::crossfadeMs);
    for (auto* slot : { &a, &b })
    {
        CHECK (slot->crossfade() == 1.0f);
        CHECK_FALSE (slot->showsOutgoing());
        CHECK (slot->primary().getAlpha() == 1.0f);
        CHECK (slot->companion().getAlpha() == 1.0f);
    }
    // Nothing moved, during the crossfade or after it.
    CHECK (boundsOf() == during);
}

TEST_CASE ("Under reduced motion a Style change swaps the Compact Slots' controls at once, with no crossfade")
{
    host::EditorHost host;
    showCompactView (host);
    host.settleAnimations();
    auto& body = host.expandedView().compactView();
    body.setReducedMotion (true);
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    REQUIRE (host.settleUntil ([&] { return body.compactSlot (0).primary().getTitle() == "Colour"; }));
    // On the tick that rebinds them: nothing easing, every control at full alpha.
    CHECK (staple::Tween::numberEasing() == 0);
    for (size_t i = 0; i < 2; ++i)
    {
        auto& slot = body.compactSlot (i);
        CAPTURE (i);
        CHECK (slot.crossfade() == 1.0f);
        CHECK_FALSE (slot.showsOutgoing());
        CHECK (slot.primary().getAlpha() == 1.0f);
        CHECK (slot.companion().getAlpha() == 1.0f);
    }
}

TEST_CASE ("A second Style change during the Compact Slots' crossfade lands cleanly on the newest Style")
{
    host::EditorHost host;
    showCompactView (host);
    host.settleAnimations();
    auto& body = host.expandedView().compactView();
    auto& a = body.compactSlot (0);
    auto& b = body.compactSlot (1);
    REQUIRE (host.processor.chooseStyle (comp1::Style::FET));
    REQUIRE (host.settleUntil ([&] { return b.companion().getTitle() == "Hold"; }));
    REQUIRE (a.crossfade() < 1.0f);
    // Mid-crossfade, Drums: it fades out from FET part-way in, toward Drums.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Drums));
    REQUIRE (host.settleUntil ([&] { return b.primary().getTitle() == "Drive"; }));
    CHECK (a.crossfade() < 1.0f);
    CHECK (a.showsOutgoing());

    host.settleAnimations();
    const std::array<juce::Component*, 4> shown { &a.primary(), &a.companion(), &b.primary(), &b.companion() };
    const std::array<const char*, 4> drums { "Attack", "Hold", "Drive", "Saturation" };
    for (size_t i = 0; i < shown.size(); ++i)
    {
        CAPTURE (i);
        CHECK (shown[i]->getTitle() == drums[i]);
        CHECK (shown[i]->getAlpha() == 1.0f);
        CHECK (shown[i]->isVisible());
    }
    for (auto* slot : { &a, &b })
    {
        CHECK (slot->crossfade() == 1.0f);
        CHECK_FALSE (slot->showsOutgoing());
    }
}
