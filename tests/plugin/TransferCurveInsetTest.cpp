// The Transfer Curve inset on the Expanded View's Level tab: where it shows, that it draws what the Transfer Curve Graph
// draws, its Threshold and Ratio handles, their drags and the keys, with Comp Follow (ADR 0004).

#include "Playing.h"
#include "TransferCurveInset.h"
#include "comp1/Style.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <map>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;
using comp1::DisplayTab;

namespace
{

struct InsetHost : host::Playing
{
    InsetHost() { showTab (DisplayTab::level); }

    comp1::Display& display() { return expandedView().display(); }
    comp1::TransferCurveInset& inset() { return display().transferCurveInset(); }

    void showTab (DisplayTab tab)
    {
        processor.setDisplayTab (tab);
        REQUIRE (settleUntil ([&] { return display().shownTab() == tab; }));
    }
};

} // namespace

TEST_CASE ("The Transfer Curve inset shows in the Level graph's top-left corner on the Level tab only, in the Expanded View only, 160 px square at 100% and scaled with UI Scale")
{
    InsetHost host;
    auto& inset = host.inset();
    auto& graph = host.display().levelGraph();
    CHECK (inset.isShowing());
    CHECK (inset.getWidth() == comp1::TransferCurveInset::side);
    CHECK (inset.getHeight() == comp1::TransferCurveInset::side);
    CHECK (comp1::TransferCurveInset::side == 160);
    // Over the graph's top-left corner, inside it.
    const auto inGraph = graph.getLocalArea (&inset, inset.getLocalBounds());
    CHECK (graph.getLocalBounds().contains (inGraph));
    CHECK (inGraph.getX() < graph.getWidth() / 4);
    CHECK (inGraph.getY() < graph.getHeight() / 4);

    for (const auto tab : { DisplayTab::detectionEq, DisplayTab::transferCurve })
    {
        host.showTab (tab);
        CHECK_FALSE (inset.isShowing());
    }
    host.showTab (DisplayTab::level);
    CHECK (inset.isShowing());

    // In the Compact View: not shown.
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::compact);
    CHECK_FALSE (inset.isShowing());
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::expanded);
    CHECK (inset.isShowing());

    // At 200%, twice as big on screen.
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (200);
    host.settle();
    CHECK (host.inset().getScreenBounds().getWidth() == 2 * comp1::TransferCurveInset::side);
}

TEST_CASE ("The Transfer Curve inset draws what the Transfer Curve Graph draws, at every Threshold, Ratio and Knee", "[sweep]")
{
    InsetHost host;
    auto& inset = host.inset().graph();
    auto& tab = host.display().transferCurveGraph();
    for (const float threshold : { -60.0f, -30.0f, -10.0f })
        for (const float ratio : { 1.0f, 2.0f, 4.0f, std::numeric_limits<float>::infinity() })
            for (const float knee : { 0.0f, 12.0f, 72.0f })
            {
                CAPTURE (threshold, ratio, knee);
                host.edit (parameters::thresholdId, threshold);
                host.edit (parameters::ratioId, ratio);
                host.edit (parameters::kneeId, knee);
                REQUIRE (host.settleUntil ([&] {
                    return juce::exactlyEqual (inset.curve().kneeDb, static_cast<double> (knee))
                           && juce::exactlyEqual (inset.curve().thresholdDb, static_cast<double> (host.value (parameters::thresholdId)))
                           && juce::exactlyEqual (tab.curve().kneeDb, static_cast<double> (knee))
                           && juce::exactlyEqual (tab.curve().thresholdDb, static_cast<double> (host.value (parameters::thresholdId)));
                }));
                CHECK (inset.curve().inverseRatio == tab.curve().inverseRatio);
                CHECK (inset.curve().rangeDb == tab.curve().rangeDb);
                for (int input = -60; input <= 0; ++input)
                {
                    CAPTURE (input);
                    CHECK (inset.outputAt (input) == tab.outputAt (input));
                }
                CHECK (host.inset().valueText() == tab.settingsText());
            }
}

namespace
{

using comp1::InsetHandle;

// A handle's centre, in its own pixels.
juce::Point<float> centreOf (comp1::TransferCurveInset& inset, InsetHandle which)
{
    return inset.handleCentre (which) - inset.handle (which).getPosition().toFloat();
}

// A press on a handle's centre and a drag by offset (halfway, then all of it), through its own handlers
// (CODING_STANDARDS "Editor behaviour"), held: the release is the caller's.
void pressAndDrag (comp1::TransferCurveInset& inset, InsetHandle which, juce::Point<float> offset, juce::ModifierKeys mods = {})
{
    auto& handle = inset.handle (which);
    const auto from = centreOf (inset, which);
    const auto held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    handle.mouseDown (harness::mouseEvent (handle, from, held));
    handle.mouseDrag (harness::mouseEvent (handle, from + offset * 0.5f, held, from));
    handle.mouseDrag (harness::mouseEvent (handle, from + offset, held, from));
}

// The same, and the release.
void dragHandle (comp1::TransferCurveInset& inset, InsetHandle which, juce::Point<float> offset, juce::ModifierKeys mods = {})
{
    pressAndDrag (inset, which, offset, mods);
    auto& handle = inset.handle (which);
    const auto from = centreOf (inset, which);
    handle.mouseUp (harness::mouseEvent (handle, from + offset, mods, from));
}

// Where the handles belong, from the inset's top left: on the drawn curve at input = Threshold, and at input 0 dB,
// at the graph's own scale.
juce::Point<float> expectedCentre (comp1::TransferCurveInset& inset, InsetHandle which)
{
    auto& graph = inset.graph();
    const double input = which == InsetHandle::threshold ? graph.curve().thresholdDb : 0.0;
    const double w = graph.getWidth(), h = graph.getHeight();
    return graph.getPosition().toFloat()
           + juce::Point<double> ((input + 60.0) / 60.0 * w, -graph.outputAt (input) / 60.0 * h).toFloat();
}

void checkHandlesOnCurve (comp1::TransferCurveInset& inset)
{
    for (const auto which : { InsetHandle::threshold, InsetHandle::ratio })
    {
        CAPTURE (which == InsetHandle::threshold ? "Threshold handle" : "Ratio handle");
        const auto at = inset.handleCentre (which), expected = expectedCentre (inset, which);
        CHECK_THAT (at.x, WithinAbs (expected.x, 1.0e-3));
        CHECK_THAT (at.y, WithinAbs (expected.y, 1.0e-3));
    }
}

// Ratio's place in its parameter's travel, 0 to 1.
float ratioTravel (host::EditorHost& host) { return host.parameter (parameters::ratioId).getValue(); }

// Every host parameter's plain value, by ID.
std::map<juce::String, float> allValues (host::EditorHost& host)
{
    std::map<juce::String, float> values;
    for (auto* p : host.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            values[ranged->getParameterID()] = host.value (ranged->getParameterID());
    return values;
}

} // namespace

TEST_CASE ("The Transfer Curve inset's Threshold handle sits on the curve at the knee and its Ratio handle at the curve's right end, at every Threshold, Ratio and Knee", "[sweep]")
{
    InsetHost host;
    auto& inset = host.inset();
    for (const float threshold : { -60.0f, -45.0f, -24.0f, -6.0f, 0.0f })
        for (const float ratio : { 1.0f, 1.5f, 4.0f, 20.0f, std::numeric_limits<float>::infinity() })
            for (const float knee : { 0.0f, 12.0f, 72.0f })
            {
                CAPTURE (threshold, ratio, knee);
                host.edit (parameters::thresholdId, threshold);
                host.edit (parameters::ratioId, ratio);
                host.edit (parameters::kneeId, knee);
                REQUIRE (host.settleUntil ([&] {
                    const auto& drawn = inset.graph().curve();
                    return juce::exactlyEqual (drawn.kneeDb, static_cast<double> (knee))
                           && juce::exactlyEqual (drawn.thresholdDb, static_cast<double> (host.value (parameters::thresholdId)));
                }));
                checkHandlesOnCurve (inset);
                // The knee across at Threshold's place on the input axis, the end at the curve's right edge.
                const auto graphLeft = static_cast<float> (inset.graph().getX());
                const auto width = static_cast<float> (inset.graph().getWidth());
                CHECK_THAT (inset.handleCentre (InsetHandle::threshold).x, WithinAbs (graphLeft + (threshold + 60.0f) / 60.0f * width, 1.0e-3));
                CHECK_THAT (inset.handleCentre (InsetHandle::ratio).x, WithinAbs (graphLeft + width, 1.0e-3));
            }
}

TEST_CASE ("The Transfer Curve inset's handles are 6 px discs at 100% UI Scale, grabbed a little beyond their edge, and scale with UI Scale")
{
    InsetHost host;
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    auto& inset = host.inset();
    host.settle();
    CHECK (comp1::TransferCurveInset::handleRadius == 6.0f);
    CHECK (comp1::TransferCurveInset::handleHitRadius > comp1::TransferCurveInset::handleRadius);
    CHECK (comp1::TransferCurveInset::handleHitRadius <= 2.0f * comp1::TransferCurveInset::handleRadius);
    for (const auto which : { InsetHandle::threshold, InsetHandle::ratio })
    {
        CHECK (inset.lookOf (which).radius == comp1::TransferCurveInset::handleRadius);
        auto& handle = inset.handle (which);
        const auto centre = centreOf (inset, which);
        // A press on the disc's edge, and a little beyond it, grabs it; one well beyond doesn't.
        const auto hits = [&] (float dx) { return handle.hitTest (juce::roundToInt (centre.x + dx - 0.5f), juce::roundToInt (centre.y - 0.5f)); };
        CHECK (hits (0.0f));
        CHECK (hits (comp1::TransferCurveInset::handleRadius));
        CHECK (hits (comp1::TransferCurveInset::handleHitRadius - 1.0f));
        CHECK_FALSE (hits (comp1::TransferCurveInset::handleHitRadius + 1.0f));
    }
    const int atFull = inset.handle (InsetHandle::threshold).getScreenBounds().getWidth();
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (200);
    host.settle();
    CHECK (host.inset().handle (InsetHandle::threshold).getScreenBounds().getWidth() == 2 * atFull);
}

TEST_CASE ("Dragging the Transfer Curve inset's Threshold handle moves only Threshold, across at the input axis's scale, and the Ratio handle only Ratio, by the knobs' travel with up lowering it, Shift a quarter as far, each drag one undo step inside gestures")
{
    InsetHost host;
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 6.0f);
    auto& inset = host.inset();
    host.settle();
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    // The curve's width spans the 60 dB input axis.
    REQUIRE (inset.graph().getWidth() == comp1::TransferCurveInset::side - 2 * comp1::TransferCurveInset::padding);
    const double dbPerPixel = 60.0 / inset.graph().getWidth();

    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    // The Threshold handle right and down: Threshold up by the dB the horizontal pixels span, Ratio untouched.
    const auto before = allValues (host);
    dragHandle (inset, InsetHandle::threshold, { 37.0f, 30.0f });
    CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (-30.0 + 37.0 * dbPerPixel, 1.0e-3));
    auto after = allValues (host);
    after.erase (parameters::thresholdId);
    auto expected = before;
    expected.erase (parameters::thresholdId);
    CHECK (after == expected);
    CHECK (history.undoSteps() == start + 1);
    // The handle followed, on the curve.
    host.settle();
    checkHandlesOnCurve (inset);

    // The Ratio handle down and left: Ratio up, 40 px of the knobs' 200 px travel, Threshold untouched.
    const float travel = ratioTravel (host);
    const auto beforeRatio = allValues (host);
    dragHandle (inset, InsetHandle::ratio, { -25.0f, 40.0f });
    CHECK_THAT (ratioTravel (host), WithinAbs (travel + 0.2f, 1.0e-5));
    CHECK (host.value (parameters::ratioId) > 4.0f);
    after = allValues (host);
    after.erase (parameters::ratioId);
    expected = beforeRatio;
    expected.erase (parameters::ratioId);
    CHECK (after == expected);
    CHECK (history.undoSteps() == start + 2);

    // Up with Shift: a quarter as far; up lowers Ratio.
    const float travelNow = ratioTravel (host);
    dragHandle (inset, InsetHandle::ratio, { 0.0f, -40.0f }, juce::ModifierKeys::shiftModifier);
    CHECK_THAT (ratioTravel (host), WithinAbs (travelNow - 0.05f, 1.0e-5));
    CHECK (history.undoSteps() == start + 3);

    // Left with Shift: a quarter as far.
    const float thresholdNow = host.value (parameters::thresholdId);
    dragHandle (inset, InsetHandle::threshold, { -40.0f, 0.0f }, juce::ModifierKeys::shiftModifier);
    CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (thresholdNow - 10.0 * dbPerPixel, 1.0e-3));
    CHECK (history.undoSteps() == start + 4);
    host.processor.removeListener (&recorder);

    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }
    CHECK (recorder.open.empty());

    // Each drag undoes on its own.
    for (int i = 0; i < 4; ++i)
        history.undo();
    CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (-30.0f, 1.0e-4));
    CHECK_THAT (host.value (parameters::ratioId), WithinAbs (4.0f, 1.0e-4));
}

TEST_CASE ("Only the Transfer Curve inset's handles take a press: elsewhere in the inset a press passes through and changes nothing")
{
    InsetHost host;
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    auto& inset = host.inset();
    host.settle();
    auto& display = host.display();
    const auto pressedAt = [&] (juce::Point<float> inInset) {
        return display.getComponentAt (display.getLocalPoint (&inset, inInset).roundToInt());
    };
    // On a handle: that handle.
    for (const auto which : { InsetHandle::threshold, InsetHandle::ratio })
        CHECK (pressedAt (inset.handleCentre (which)) == &inset.handle (which));

    // Anywhere else in the inset: what lies under it, not the inset or anything in it.
    int checked = 0;
    for (float y = 2.0f; y < static_cast<float> (inset.getHeight()); y += 6.0f)
        for (float x = 2.0f; x < static_cast<float> (inset.getWidth()); x += 6.0f)
        {
            const juce::Point<float> at { x, y };
            if (at.getDistanceFrom (inset.handleCentre (InsetHandle::threshold)) <= comp1::TransferCurveInset::handleHitRadius + 1.5f
                || at.getDistanceFrom (inset.handleCentre (InsetHandle::ratio)) <= comp1::TransferCurveInset::handleHitRadius + 1.5f)
                continue;
            CAPTURE (x, y);
            auto* target = pressedAt (at);
            CHECK (target != &inset);
            CHECK_FALSE (inset.isParentOf (target));
            ++checked;
        }
    CHECK (checked > 500);

    // A drag on the inset itself, as a window would send one if the inset took it, changes nothing.
    const auto before = allValues (host);
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();
    const juce::Point<float> from { 20.0f, 120.0f };
    const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    inset.mouseDown (harness::mouseEvent (inset, from, held));
    inset.mouseDrag (harness::mouseEvent (inset, from + juce::Point<float> { 30.0f, 30.0f }, held, from));
    inset.mouseUp (harness::mouseEvent (inset, from + juce::Point<float> { 30.0f, 30.0f }, {}, from));
    CHECK (allValues (host) == before);
    CHECK (history.undoSteps() == steps);
}

TEST_CASE ("A Transfer Curve inset handle lights while hovered, shows its direction's resize cursor, and while dragged is selected with a readout card of its setting")
{
    InsetHost host;
    host.edit (parameters::thresholdId, -18.0f);
    host.edit (parameters::ratioId, 4.0f);
    auto& inset = host.inset();
    host.settle();
    CHECK (inset.handle (InsetHandle::threshold).getMouseCursor() == juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor));
    CHECK (inset.handle (InsetHandle::ratio).getMouseCursor() == juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor));

    for (const auto which : { InsetHandle::threshold, InsetHandle::ratio })
    {
        const auto other = which == InsetHandle::threshold ? InsetHandle::ratio : InsetHandle::threshold;
        auto& handle = inset.handle (which);
        const auto centre = centreOf (inset, which);
        CHECK_FALSE (inset.lookOf (which).hovered);
        // A test window isn't in front, so the pointer's events are sent to the handle's handlers directly
        // (CODING_STANDARDS "Editor behaviour").
        auto& repaints = harness::countRepaintsOf (handle);
        handle.mouseEnter (harness::mouseEvent (handle, centre));
        CHECK (inset.lookOf (which).hovered);
        CHECK_FALSE (inset.lookOf (other).hovered);
        CHECK (repaints.count > 0);
        handle.mouseExit (harness::mouseEvent (handle, centre + juce::Point<float> { 30.0f, 0.0f }));
        CHECK_FALSE (inset.lookOf (which).hovered);
        handle.setCachedComponentImage (nullptr);

        CHECK_FALSE (inset.lookOf (which).selected);
        CHECK_FALSE (inset.readout().has_value());
        pressAndDrag (inset, which, {});
        CHECK (inset.lookOf (which).selected);
        CHECK_FALSE (inset.lookOf (other).selected);
        REQUIRE (inset.readout().has_value());
        const juce::String minus { juce::CharPointer_UTF8 ("\xe2\x88\x92") };
        if (which == InsetHandle::threshold)
        {
            CHECK (inset.readout()->title == "Threshold");
            CHECK (inset.readout()->value == minus + "18.00 dB");
        }
        else
        {
            CHECK (inset.readout()->title == "Ratio");
            CHECK (inset.readout()->value == "4:1");
        }
        auto& handleNow = inset.handle (which);
        handleNow.mouseUp (harness::mouseEvent (handleNow, centreOf (inset, which)));
        CHECK_FALSE (inset.lookOf (which).selected);
        CHECK_FALSE (inset.readout().has_value());
    }
}

TEST_CASE ("Grabbing a Transfer Curve inset handle while Comp Follow is on turns it off at the followed values, then the edit, as one undo step, and undo follows again")
{
    InsetHost host;
    // Opto at Comp 80%, the stored Threshold, Ratio and Auto Gain moved well off the Comp Curve by Host Automation.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compId, 80.0f);
    host.set (parameters::thresholdId, -6.0f);
    host.set (parameters::ratioId, 1.5f);
    host.set (parameters::autoGainId, 0.0f);
    host.settle();
    const auto before = allValues (host);
    auto& thresholdParameter = host.parameter (parameters::thresholdId);
    auto& ratioParameter = host.parameter (parameters::ratioId);
    const float followedThreshold = thresholdParameter.convertTo0to1 (static_cast<float> (host.processor.followedThresholdDb()));
    const float followedRatio = ratioParameter.convertTo0to1 (static_cast<float> (host.processor.followedRatio()));
    auto& history = host.processor.session().editHistory();
    const int steps = history.undoSteps();
    const double dbPerPixel = 60.0 / host.inset().graph().getWidth();

    const auto how = GENERATE (as<juce::String> {}, "press", "Threshold drag", "Ratio drag", "key");
    CAPTURE (how);
    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    if (how == "press")
        dragHandle (host.inset(), InsetHandle::threshold, {});
    else if (how == "Threshold drag")
        dragHandle (host.inset(), InsetHandle::threshold, { 20.0f, 20.0f });
    else if (how == "Ratio drag")
        dragHandle (host.inset(), InsetHandle::ratio, { 20.0f, 20.0f });
    else
    {
        host.inset().grabKeyboardFocus();
        CHECK (host.press (juce::KeyPress (juce::KeyPress::rightKey)));
    }
    host.processor.removeListener (&recorder);

    CHECK (host.value (parameters::compFollowId) == 0.0f);
    CHECK (host.value (parameters::autoGainId) == 1.0f);
    // The edit carried on from the followed values.
    if (how == "press")
    {
        CHECK_THAT (thresholdParameter.getValue(), WithinAbs (followedThreshold, 1.0e-6));
        CHECK_THAT (ratioParameter.getValue(), WithinAbs (followedRatio, 1.0e-6));
    }
    else if (how == "Threshold drag")
    {
        CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (host.processor.followedThresholdDb() + 20.0 * dbPerPixel, 1.0e-3));
        CHECK_THAT (ratioParameter.getValue(), WithinAbs (followedRatio, 1.0e-6));
    }
    else if (how == "Ratio drag")
    {
        CHECK_THAT (thresholdParameter.getValue(), WithinAbs (followedThreshold, 1.0e-6));
        CHECK_THAT (ratioParameter.getValue(), WithinAbs (followedRatio + 0.1f, 1.0e-5));
    }
    else
    {
        CHECK_THAT (thresholdParameter.getValue(), WithinAbs (followedThreshold + 0.01f, 1.0e-5));
        CHECK_THAT (ratioParameter.getValue(), WithinAbs (followedRatio, 1.0e-6));
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
    CHECK (host.value (parameters::compFollowId) == 1.0f);
}

TEST_CASE ("Arrows on the focused Transfer Curve inset step Threshold and Ratio by 1% of their travel, 0.2% with Shift, a held key one undo step")
{
    InsetHost host;
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::ratioId, 4.0f);
    auto& inset = host.inset();
    inset.grabKeyboardFocus();
    REQUIRE (inset.hasKeyboardFocus (false));
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    const auto travel = [&host] (const juce::String& id) { return host.parameter (id).getValue(); };
    const float threshold = travel (parameters::thresholdId), ratio = travel (parameters::ratioId);
    const auto right = juce::KeyPress (juce::KeyPress::rightKey), left = juce::KeyPress (juce::KeyPress::leftKey);
    const auto up = juce::KeyPress (juce::KeyPress::upKey), down = juce::KeyPress (juce::KeyPress::downKey);

    CHECK (host.press (right));
    CHECK_THAT (travel (parameters::thresholdId), WithinAbs (threshold + 0.01f, 1.0e-6));
    // 0.6 dB: 1% of Threshold's 60 dB.
    CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (-29.4f, 1.0e-4));
    CHECK (host.press (harness::withShift (left)));
    CHECK_THAT (travel (parameters::thresholdId), WithinAbs (threshold + 0.008f, 1.0e-6));
    // Down raises Ratio, up lowers it, as a drag does.
    CHECK (host.press (down));
    CHECK_THAT (travel (parameters::ratioId), WithinAbs (ratio + 0.01f, 1.0e-6));
    CHECK (host.press (harness::withShift (up)));
    CHECK_THAT (travel (parameters::ratioId), WithinAbs (ratio + 0.008f, 1.0e-6));
    CHECK (history.undoSteps() == start + 4);

    // A held key: its repeats, then the release, one step.
    REQUIRE (host.hold (down));
    REQUIRE (host.hold (down));
    REQUIRE (host.hold (down));
    host.release();
    CHECK_THAT (travel (parameters::ratioId), WithinAbs (ratio + 0.038f, 1.0e-6));
    CHECK (history.undoSteps() == start + 5);

    while (history.undoSteps() > start)
        history.undo();
    CHECK_THAT (host.value (parameters::thresholdId), WithinAbs (-30.0f, 1.0e-4));
    CHECK_THAT (host.value (parameters::ratioId), WithinAbs (4.0f, 1.0e-4));
}

TEST_CASE ("The Transfer Curve inset is a slider titled Transfer Curve inset, read with Threshold, Ratio and Knee as the Transfer Curve Graph reads them, a Tab stop after the display's tabs; its graph and handles aren't other elements")
{
    InsetHost host;
    host.edit (parameters::thresholdId, -18.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 6.0f);
    auto& inset = host.inset();
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (inset.graph().curve().kneeDb, 6.0); }));
    auto* handler = inset.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::slider);
    CHECK (handler->getTitle() == "Transfer Curve inset");
    REQUIRE (handler->getValueInterface() != nullptr);
    const juce::String minus { juce::CharPointer_UTF8 ("\xe2\x88\x92") };
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "Threshold " + minus + "18.00 dB, Ratio 4:1, Knee 6.0 dB");
    CHECK_FALSE (inset.graph().isAccessible());
    CHECK (inset.getWantsKeyboardFocus());
    for (const auto which : { InsetHandle::threshold, InsetHandle::ratio })
    {
        CHECK_FALSE (inset.handle (which).isAccessible());
        CHECK_FALSE (inset.handle (which).getWantsKeyboardFocus());
    }

    juce::Component::unfocusAllComponents();
    juce::StringArray titles;
    for (int i = 0; i < 200; ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        REQUIRE (focused != nullptr);
        if (titles.contains (focused->getTitle()))
            break;
        titles.add (focused->getTitle());
    }
    CAPTURE (titles.joinIntoString (", "));
    const int tabs = titles.indexOf ("Transfer Curve");
    REQUIRE (tabs >= 0);
    CHECK (titles[tabs + 1] == "Transfer Curve inset");
    CHECK (titles[tabs + 2] == "Output Meter");
}

TEST_CASE ("The Transfer Curve inset and its handles follow Comp, a Style, A/B and a restored session on the tick, and with no audio and no edits ask for no repaint")
{
    InsetHost host;
    auto& graph = host.inset().graph();
    const auto drawsWhatPlays = [&] {
        const bool settled = host.settleUntil ([&] {
            const auto played = host.processor.playedCurve();
            return juce::exactlyEqual (graph.curve().thresholdDb, played.thresholdDb)
                   && juce::exactlyEqual (graph.curve().inverseRatio, played.inverseRatio)
                   && juce::exactlyEqual (graph.curve().kneeDb, played.kneeDb);
        });
        checkHandlesOnCurve (host.inset());
        return settled;
    };
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    CHECK (drawsWhatPlays());
    const double optoThreshold = graph.curve().thresholdDb;
    host.set (parameters::compId, 90.0f);
    CHECK (drawsWhatPlays());
    CHECK (graph.curve().thresholdDb < optoThreshold);
    const auto onA = graph.curve();

    auto& session = host.processor.session();
    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    REQUIRE (host.processor.chooseStyle (comp1::Style::Vocal));
    CHECK (drawsWhatPlays());
    CHECK_FALSE ((juce::exactlyEqual (graph.curve().kneeDb, onA.kneeDb) && juce::exactlyEqual (graph.curve().thresholdDb, onA.thresholdDb)));
    session.selectCompareSide (plugkit::ui::CompareSide::A);
    CHECK (drawsWhatPlays());
    CHECK (juce::exactlyEqual (graph.curve().thresholdDb, onA.thresholdDb));

    // A restored session, as a Preset load restores its settings.
    const auto saved = host::saved (host.processor);
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    CHECK (drawsWhatPlays());
    host::restore (host.processor, saved);
    CHECK (drawsWhatPlays());
    CHECK (juce::exactlyEqual (graph.curve().thresholdDb, onA.thresholdDb));
    CHECK (juce::exactlyEqual (graph.curve().kneeDb, onA.kneeDb));

    host.playFor (0.2, host::silence());
    auto& repaints = harness::countRepaintsOf (graph);
    auto& thresholdRepaints = harness::countRepaintsOf (host.inset().handle (InsetHandle::threshold));
    auto& ratioRepaints = harness::countRepaintsOf (host.inset().handle (InsetHandle::ratio));
    host.settle (300);
    CHECK (repaints.count == 0);
    CHECK (thresholdRepaints.count == 0);
    CHECK (ratioRepaints.count == 0);
    host.edit (parameters::kneeId, 30.0f);
    CHECK (host.settleUntil ([&] { return repaints.count > 0; }));
    graph.setCachedComponentImage (nullptr);
    host.inset().handle (InsetHandle::threshold).setCachedComponentImage (nullptr);
    host.inset().handle (InsetHandle::ratio).setCachedComponentImage (nullptr);
}
