// The Compact View's controls at work: a Style pick, Comp and Comp Follow (ADR 0004), the Gate, Output Gain into the
// Limiter, the Gain Reduction meter and the editor-wide keys.

#include "Host.h"
#include "Playing.h"
#include "comp1/Style.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

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

} // namespace

TEST_CASE ("A Style picked in the Compact View writes that Style's defaults as one undo step, and the Expanded View shows them after switching")
{
    host::EditorHost host;
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::attackId, 30.0f);
    showCompactView (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    host::EditorHost::pick (compact<juce::ComboBox> (host, "Style"), "Drums");
    CHECK (host.value (parameters::styleId) == static_cast<float> (comp1::Style::Drums));
    // Drums' defaults (comp1/Style.h): Comp Follow on and a 5 ms Attack.
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::attackId), WithinAbs (5.0f, 1.0e-4));
    CHECK (history.undoSteps() == steps + 1);

    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.settleUntil ([&] { return host.showing<juce::ComboBox> ("Style").getText() == "Drums"; }));
    CHECK (host.showing<juce::ToggleButton> ("Comp Follow").getToggleState());
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

    const juce::String how = GENERATE (as<juce::String> {}, "press", "drag", "typed", "key");
    CAPTURE (how);
    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    if (how == "press")
        host::EditorHost::drag (comp, 0.0f);
    else if (how == "drag")
        host::EditorHost::drag (comp, 20.0f);
    else if (how == "typed")
        REQUIRE (comp.commitTypedText ("60 %"));
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
    else if (how == "typed")
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

TEST_CASE ("The Compact View's Gate toggle turns the Gate on and off and its Gate Threshold moves, one undo step each")
{
    host::EditorHost host;
    showCompactView (host);
    auto& history = host.processor.session().editHistory();
    auto& gate = compact<juce::ToggleButton> (host, "Gate");
    const bool byKeys = GENERATE (false, true);
    CAPTURE (byKeys);
    int steps = history.undoSteps();
    for (const float expected : { 1.0f, 0.0f })
    {
        if (byKeys)
        {
            gate.grabKeyboardFocus();
            REQUIRE (gate.hasKeyboardFocus (false));
            CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
        }
        else
            host::EditorHost::click (gate);
        CHECK (host.value (parameters::gateId) == expected);
        CHECK (history.undoSteps() == ++steps);
    }

    const float before = host.value (parameters::gateThresholdId);
    auto& threshold = compactKnob (host, "Gate Threshold");
    if (byKeys)
    {
        threshold.grabKeyboardFocus();
        REQUIRE (threshold.hasKeyboardFocus (false));
        CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    }
    else
        host::EditorHost::drag (threshold, 20.0f);
    CHECK (host.value (parameters::gateThresholdId) > before);
    CHECK (history.undoSteps() == steps + 1);
    history.undo();
    CHECK (host.value (parameters::gateThresholdId) == before);
}

TEST_CASE ("The Compact View's Output Gain drag is one undo step, and with the Limiter on a steady tone it pushes over the Ceiling never passes it")
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
    host::EditorHost::drag (compactKnob (host, "Output Gain"), 72.0f);
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
    auto& outputGain = compactKnob (host, "Output Gain");
    host::EditorHost::drag (outputGain, 20.0f);
    const float edited = host.value (parameters::outputGainId);
    REQUIRE (edited > 0.0f);
    outputGain.grabKeyboardFocus();
    REQUIRE (outputGain.hasKeyboardFocus (false));

    CHECK (host.press (juce::KeyPress ('z', command, 0)));
    CHECK (host.value (parameters::outputGainId) == 0.0f);
    CHECK (host.press (juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0)));
    CHECK (host.value (parameters::outputGainId) == edited);
    CHECK (host.press (juce::KeyPress ('b', command, 0)));
    CHECK (host.value (parameters::globalBypassId) == 1.0f);
    CHECK (host.press (juce::KeyPress ('b', command, 0)));
    CHECK (host.value (parameters::globalBypassId) == 0.0f);
}
