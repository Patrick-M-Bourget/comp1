// Comp Follow from the Expanded View (ADR 0004): what Threshold, Ratio and Auto Gain show while following, and how
// grabbing Threshold, Ratio or Comp, or the Comp Follow toggle, switches it as one undo step.

#include "EditorHost.h"
#include "Host.h"
#include "comp1/Style.h"
#include "plugkit/ui/FooterPopoverFrame.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <map>
#include <vector>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

// Every host parameter's plain value, by ID.
std::map<juce::String, float> allValues (host::EditorHost& host)
{
    std::map<juce::String, float> values;
    for (auto* p : host.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            values[ranged->getParameterID()] = host.value (ranged->getParameterID());
    return values;
}

// What a screen reader reads as the knob's value.
juce::String spoken (staple::Knob& knob) { return knob.getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString(); }

// Opto chosen (Comp Follow on, Comp 50%), Comp moved to 80% (the Comp Curve's -32.4 dB and 4.29:1), the stored
// Threshold and Ratio moved well off the curve by Host Automation, which following ignores, and Auto Gain turned off.
void followingOffTheCurve (host::EditorHost& host)
{
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compId, 80.0f);
    host.set (parameters::thresholdId, -6.0f);
    host.set (parameters::ratioId, 1.5f);
    host.set (parameters::autoGainId, 0.0f);
    host.settle();
}

// What a host parameter holds after the trip through its normalised range.
float asStored (host::EditorHost& host, const juce::String& id, double plain)
{
    auto& p = host.parameter (id);
    return p.convertFrom0to1 (p.convertTo0to1 (static_cast<float> (plain)));
}

// The knobs show the followed values, to the float a knob's range holds them in.
bool showsFollowed (host::EditorHost& host)
{
    const auto near = [] (double shown, double followed) { return std::abs (shown - followed) <= 1.0e-5 * std::abs (followed); };
    return near (host.knob ("Threshold").getValue(), host.processor.followedThresholdDb())
           && near (host.knob ("Ratio").getValue(), host.processor.followedRatio());
}

const juce::String minus { juce::CharPointer_UTF8 ("\xe2\x88\x92") };

} // namespace

TEST_CASE ("While Comp Follow is on, Threshold and Ratio show and read the followed values, moving with Comp by drag and by Host Automation, and not with the stored ones")
{
    host::EditorHost host;
    followingOffTheCurve (host);
    auto& threshold = host.knob ("Threshold");
    auto& ratio = host.knob ("Ratio");
    REQUIRE (host.settleUntil ([&] { return showsFollowed (host); }));
    CHECK_THAT (threshold.getValue(), WithinAbs (-32.4, 1.0e-4));
    CHECK_THAT (ratio.getValue(), WithinAbs (4.29, 0.005));
    CHECK (spoken (threshold) == minus + "32.40 dB");
    CHECK (threshold.tooltipValue() == spoken (threshold));
    CHECK (spoken (ratio) == "4.29:1");
    CHECK (ratio.tooltipValue() == spoken (ratio));

    // Comp dragged: they follow it.
    host::EditorHost::drag (host.knob ("Comp"), -40.0f);
    CHECK (host.value (parameters::compId) < 80.0f);
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    REQUIRE (host.settleUntil ([&] { return showsFollowed (host); }));
    CHECK (threshold.getValue() > -32.4);

    // Comp's Host Automation: they follow it.
    host.set (parameters::compId, 95.0f);
    REQUIRE (host.settleUntil ([&] { return showsFollowed (host); }));
    CHECK (threshold.getValue() < -32.4);
    const double shownThreshold = threshold.getValue(), shownRatio = ratio.getValue();

    // The stored Threshold's and Ratio's Host Automation: they stay where they are.
    host.set (parameters::thresholdId, -50.0f);
    host.set (parameters::ratioId, 2.0f);
    host.settle (200);
    CHECK (juce::exactlyEqual (threshold.getValue(), shownThreshold));
    CHECK (juce::exactlyEqual (ratio.getValue(), shownRatio));
}

TEST_CASE ("With Comp Follow on or off, the output popover's Auto Gain is enabled, shows its own parameter with its usual tooltip, and turns it on and off")
{
    host::EditorHost host;
    followingOffTheCurve (host);
    const bool following = GENERATE (true, false);
    CAPTURE (following);
    if (! following)
        host.edit (parameters::compFollowId, 0.0f);
    host.openOutputPopover();
    auto& autoGain = host.showing<plugkit::ui::OutputToggle> ("Auto Gain");
    const auto shows = [&] (bool on) { return host.settleUntil ([&] { return autoGain.getToggleState() == on; }); };
    host.settle();
    CHECK (autoGain.isEnabled());
    CHECK (shows (false));
    CHECK (autoGain.getTooltip().startsWith ("Auto Gain: compensates the output level"));

    host::EditorHost::click (autoGain);
    CHECK (host.value (parameters::autoGainId) == 1.0f);
    CHECK (shows (true));
    host::EditorHost::click (autoGain);
    CHECK (host.value (parameters::autoGainId) == 0.0f);
    CHECK (shows (false));
    // Comp Follow switched under it leaves it showing its own.
    host.edit (parameters::compFollowId, following ? 0.0f : 1.0f);
    host.settle();
    CHECK (autoGain.isEnabled());
    CHECK (shows (false));
}

TEST_CASE ("Grabbing Threshold or Ratio while following turns Comp Follow off: the followed values, then the edit, Auto Gain left as it is, as one undo step inside gestures, and undo follows again")
{
    host::EditorHost host;
    followingOffTheCurve (host);
    REQUIRE (host.settleUntil ([&] { return showsFollowed (host); }));
    const auto before = allValues (host);
    const double followedThreshold = host.processor.followedThresholdDb(), followedRatio = host.processor.followedRatio();
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    const juce::String how = GENERATE (as<juce::String> {}, "Threshold drag", "Ratio drag", "Threshold typed", "Ratio key");
    CAPTURE (how);
    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    if (how == "Threshold drag")
        host::EditorHost::drag (host.knob ("Threshold"), 20.0f);
    else if (how == "Ratio drag")
        host::EditorHost::drag (host.knob ("Ratio"), 20.0f);
    else if (how == "Threshold typed")
        REQUIRE (host.knob ("Threshold").commitTypedText ("-20 dB"));
    else
    {
        host.knob ("Ratio").grabKeyboardFocus();
        CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    }
    host.processor.removeListener (&recorder);

    CHECK (host.value (parameters::compFollowId) == 0.0f);
    CHECK (host.value (parameters::autoGainId) == 0.0f);
    // The edit carried on from the followed value: the other of the two is the followed one, the edited one moved from
    // it the way the edit went.
    const bool thresholdEdited = how.startsWith ("Threshold");
    if (thresholdEdited)
    {
        CHECK (host.value (parameters::ratioId) == asStored (host, parameters::ratioId, followedRatio));
        CHECK (host.value (parameters::thresholdId) > followedThreshold);
        if (how == "Threshold typed")
            CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (-20.0f, 1.0e-4));
    }
    else
    {
        CHECK (host.value (parameters::thresholdId) == asStored (host, parameters::thresholdId, followedThreshold));
        CHECK (host.value (parameters::ratioId) > followedRatio);
        // A key steps 1% of the travel.
        if (how == "Ratio key")
            CHECK (host.value (parameters::ratioId) < followedRatio + 0.5);
    }
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }

    history.undo();
    CHECK (allValues (host) == before);
    REQUIRE (host.settleUntil ([&] { return showsFollowed (host); }));
}

TEST_CASE ("Grabbing Comp while Comp Follow is off follows: Comp nearest the stored Threshold, then the drag, as one undo step")
{
    host::EditorHost host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::thresholdId, -32.4f);
    const auto before = allValues (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    // A press without a drag: Comp at the Comp Curve's -32.4 dB, Opto's 80% (CompParameterTest's).
    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    host::EditorHost::drag (host.knob ("Comp"), 0.0f);
    host.processor.removeListener (&recorder);
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::compId), WithinAbs (80.0f, 0.05));
    CHECK (history.undoSteps() == steps + 1);
    for (const auto& [index, inGesture] : recorder.changes)
        CHECK (inGesture);
    history.undo();
    CHECK (allValues (host) == before);

    // And a drag: from there, as one step.
    host::EditorHost::drag (host.knob ("Comp"), 20.0f);
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    // 20 px of the knob's 200 px for 0 to 100%.
    CHECK_THAT (host.value (parameters::compId), WithinAbs (90.0f, 0.1));
    CHECK (history.undoSteps() == steps + 1);
    history.undo();
    CHECK (allValues (host) == before);
}

TEST_CASE ("An arrow key on Comp while Comp Follow is off follows: Comp nearest the stored Threshold, then the step from there, as one undo step")
{
    host::EditorHost host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::thresholdId, -32.4f);
    const auto before = allValues (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    auto& comp = host.knob ("Comp");
    comp.grabKeyboardFocus();
    REQUIRE (comp.hasKeyboardFocus (false));
    CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    host.processor.removeListener (&recorder);

    CHECK (host.value (parameters::compFollowId) == 1.0f);
    // One step up from Opto's 80% at -32.4 dB (CompParameterTest's), not from the 50% before the key.
    CHECK (host.value (parameters::compId) > 80.0f);
    CHECK (host.value (parameters::compId) < 82.0f);
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }
    history.undo();
    CHECK (allValues (host) == before);
}

TEST_CASE ("A screen reader's set of Comp while Comp Follow is off follows: Comp nearest the stored Threshold, then the value set, as one undo step")
{
    host::EditorHost host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::thresholdId, -32.4f);
    const auto before = allValues (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    auto* handler = host.knob ("Comp").getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getValueInterface() != nullptr);
    handler->getValueInterface()->setValueAsString ("60 %");
    host.processor.removeListener (&recorder);

    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::compId), WithinAbs (60.0f, 1.0e-4));
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }
    history.undo();
    CHECK (allValues (host) == before);
}

TEST_CASE ("The Comp Follow toggle turns it on through followComp() and off through stopFollowingComp(), one undo step each")
{
    host::EditorHost host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::thresholdId, -32.4f);
    host.edit (parameters::autoGainId, 0.0f);
    auto& toggle = host.showing<juce::ToggleButton> ("Comp Follow");
    REQUIRE (host.settleUntil ([&] { return ! toggle.getToggleState(); }));
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();

    const bool byKeys = GENERATE (false, true);
    CAPTURE (byKeys);
    const auto press = [&] {
        if (byKeys)
        {
            toggle.grabKeyboardFocus();
            CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
        }
        else
            host::EditorHost::click (toggle);
    };

    // On: Comp moved to the stored Threshold's place on the Comp Curve.
    press();
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::compId), WithinAbs (80.0f, 0.05));
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE (host.settleUntil ([&] { return toggle.getToggleState(); }));

    // Off: the followed values written, so the sound doesn't change, and Auto Gain left off.
    host.edit (parameters::compId, 60.0f);
    const double followedThreshold = host.processor.followedThresholdDb();
    const int stepsOn = history.undoSteps();
    press();
    CHECK (host.value (parameters::compFollowId) == 0.0f);
    CHECK (host.value (parameters::thresholdId) == asStored (host, parameters::thresholdId, followedThreshold));
    CHECK (host.value (parameters::autoGainId) == 0.0f);
    CHECK (history.undoSteps() == stepsOn + 1);
    REQUIRE (host.settleUntil ([&] { return ! toggle.getToggleState(); }));
}

TEST_CASE ("Unfollowing from the editor doesn't change the output: the Gain Reduction across the grab is following's")
{
    // A steady sine 20 dB over Opto's Comp Curve Threshold at 80%, following throughout, against grabbing Threshold
    // (a press without a drag) part-way through.
    const auto play = [] (bool grab) {
        host::EditorHost host;
        host.editor.reset();
        harness::useLayout (host.processor, juce::AudioChannelSet::mono());
        host.reopen();
        followingOffTheCurve (host);
        std::vector<float> output;
        const auto heard = [&output] (const juce::AudioBuffer<float>& buffer) {
            output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
        };
        const auto sineFrom = [] (int from) {
            return [from] (int n) { return 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (from + n) / 48000.0); };
        };
        // 30 blocks, the grab, then 30 more.
        host.play (30, 512, sineFrom (0), host::EditorHost::silent, heard);
        if (grab)
            host::EditorHost::drag (host.knob ("Threshold"), 0.0f);
        host.play (30, 512, sineFrom (30 * 512), host::EditorHost::silent, heard);
        if (grab)
            CHECK (host.value (parameters::compFollowId) == 0.0f);
        return output;
    };
    const auto following = play (false), grabbed = play (true);
    REQUIRE (following.size() == grabbed.size());
    double most = 0.0;
    for (size_t i = 0; i < following.size(); ++i)
        most = std::max (most, std::abs (static_cast<double> (following[i]) - grabbed[i]));
    // CompParameterTest's tolerance: the stored Threshold and Ratio, as floats, move the Gain Reduction by far less
    // than 0.001 dB.
    CHECK (most < 1.0e-4);
}
