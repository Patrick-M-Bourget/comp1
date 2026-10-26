// The Timing Response inset on the Expanded View's Level tab: where it shows, the response it draws and the thread that
// works it out, its Attack and Release handles, their drags and the keys. What the response holds is the Engine's
// TimingResponseTest's.

#include "Playing.h"
#include "TimingResponseInset.h"
#include "comp1/TimingResponse.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace parameters = comp1::parameters;
using comp1::DisplayTab;
using comp1::TimingHandle;

namespace
{

struct TimingHost : host::Playing
{
    explicit TimingHost (double sampleRate = 48000.0) : host::Playing (sampleRate) { showTab (DisplayTab::level); }

    comp1::Display& display() { return expandedView().display(); }
    comp1::TimingResponseInset& inset() { return display().timingResponseInset(); }

    void showTab (DisplayTab tab)
    {
        processor.setDisplayTab (tab);
        REQUIRE (settleUntil ([&] { return display().shownTab() == tab; }));
    }

    // Settles until the inset draws the response at the settings the host parameters hold.
    bool drawsWhatIsHeld()
    {
        return settleUntil ([&] {
            const auto& drawn = inset().drawn();
            return drawn.has_value() && drawn->request.settings == comp1::timingResponseShaping (processor.heldSettings());
        });
    }
};

// A handle's centre, in its own pixels.
juce::Point<float> centreOf (comp1::TimingResponseInset& inset, TimingHandle which)
{
    return inset.handleCentre (which) - inset.handle (which).getPosition().toFloat();
}

// A press on a handle's centre, a drag by offset (halfway, then all of it) and the release, through its own handlers.
void dragHandle (comp1::TimingResponseInset& inset, TimingHandle which, juce::Point<float> offset, juce::ModifierKeys mods = {})
{
    auto& handle = inset.handle (which);
    const auto from = centreOf (inset, which);
    const auto held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    handle.mouseDown (harness::mouseEvent (handle, from, held));
    handle.mouseDrag (harness::mouseEvent (handle, from + offset * 0.5f, held, from));
    handle.mouseDrag (harness::mouseEvent (handle, from + offset, held, from));
    handle.mouseUp (harness::mouseEvent (handle, from + offset, mods, from));
}

// Every host parameter's plain value, by ID.
std::map<juce::String, float> allValues (host::EditorHost& host)
{
    std::map<juce::String, float> values;
    for (auto* p : host.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            values[ranged->getParameterID()] = host.value (ranged->getParameterID());
    return values;
}

// The times a half's axis spans, in ms, worked out apart from the inset: Attack's from Attack's shortest, 5 us, to the
// burst's end, Release's from 1 ms to the response's last time.
std::pair<double, double> axisOf (TimingHandle half)
{
    if (half == TimingHandle::attack)
        return { 0.005, 1000.0 };
    return { 1.0, comp1::TimingResponse::timeMsAt (comp1::TimingResponse::points - 1) };
}

// How many pixels a decade of time spans on a half's axis: its plot's width over the axis's decades.
double pixelsPerDecade (comp1::TimingResponseInset& inset, TimingHandle half)
{
    const auto [from, to] = axisOf (half);
    return inset.plot (half).getWidth() / std::log10 (to / from);
}

} // namespace

TEST_CASE ("The Timing Response inset shows beside the Transfer Curve inset, 260 px wide and as tall, on the Level tab only, in the Expanded View only")
{
    TimingHost host;
    auto& inset = host.inset();
    auto& curveInset = host.display().transferCurveInset();
    auto& graph = host.display().levelGraph();
    CHECK (inset.isShowing());
    CHECK (inset.getWidth() == 260);
    CHECK (inset.getHeight() == comp1::TransferCurveInset::side);
    // insetGap to the right of the Transfer Curve inset, level with it, inside the graph.
    CHECK (inset.getX() == curveInset.getRight() + comp1::Display::insetGap);
    CHECK (inset.getY() == curveInset.getY());
    CHECK (graph.getLocalBounds().contains (graph.getLocalArea (&inset, inset.getLocalBounds())));

    host.showTab (DisplayTab::spectrum);
    CHECK_FALSE (inset.isShowing());
    host.showTab (DisplayTab::level);
    CHECK (inset.isShowing());

    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().shownView() == comp1::View::compact);
    CHECK_FALSE (inset.isShowing());
    host::EditorHost::click (host.expandedView().viewSwitch());
    CHECK (inset.isShowing());
}

TEST_CASE ("The Timing Response inset draws the response at the settings held, worked out at 48 kHz whatever the host plays at, on a thread other than the message thread")
{
    TimingHost host (192000.0);
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    const auto messageThread = std::this_thread::get_id();
    REQUIRE (juce::MessageManager::getInstance()->isThisTheMessageThread());
    CHECK (inset.drawn()->workedOn != messageThread);

    // Every setting that shapes it, Comp Follow's Threshold among them: a Style turns Comp Follow on.
    REQUIRE (host.processor.chooseStyle (comp1::Style::FET));
    host.edit (parameters::attackId, 2.0f);
    host.edit (parameters::releaseId, 300.0f);
    host.edit (parameters::holdId, 15.0f);
    host.edit (parameters::autoReleaseId, 1.0f);
    REQUIRE (host.processor.heldSettings().compFollow);
    REQUIRE (host.drawsWhatIsHeld());
    const auto& drawn = *inset.drawn();
    CHECK (drawn.workedOn != messageThread);
    const auto expected = comp1::timingResponse (comp1::timingResponseShaping (host.processor.heldSettings()), 48000.0);
    CHECK (drawn.response.rise == expected.rise);
    CHECK (drawn.response.recovery == expected.recovery);
    CHECK (drawn.response.burstLevelDb == expected.burstLevelDb);
}

TEST_CASE ("Dragging a Timing Response inset handle never works the response out on the message thread: the drag returns before the new response, which the inset draws when it finishes, the newest of several")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    // Several drags with no tick between: each changes Attack, and none of them works a response out.
    const auto drawnBefore = inset.drawn()->request;
    for (int i = 0; i < 5; ++i)
    {
        const float attackBefore = host.value (parameters::attackId);
        dragHandle (inset, TimingHandle::attack, { 6.0f, 0.0f });
        CHECK (host.value (parameters::attackId) > attackBefore);
        CHECK (inset.drawn()->request == drawnBefore);
    }
    // On the ticks that follow, the response at the last of them.
    REQUIRE (host.drawsWhatIsHeld());
    CHECK (inset.drawn()->workedOn != std::this_thread::get_id());
    CHECK (juce::exactlyEqual (inset.drawn()->request.settings.attackMs, static_cast<double> (host.value (parameters::attackId))));
}

TEST_CASE ("The Timing Response inset's two halves side by side, Attack's on the left from 5 us to 1 s and Release's on the right from 1 ms to about 10 s, each on a log time axis")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    const auto attack = inset.plot (TimingHandle::attack), release = inset.plot (TimingHandle::release);
    // Side by side, level, Attack's narrower, together across nearly the whole inset.
    CHECK (attack.getRight() < release.getX());
    CHECK (attack.getY() == release.getY());
    CHECK (attack.getHeight() == release.getHeight());
    CHECK (attack.getWidth() < release.getWidth());
    CHECK (attack.getWidth() + release.getWidth() > 0.85f * static_cast<float> (inset.getWidth()));
    for (const auto half : { TimingHandle::attack, TimingHandle::release })
    {
        CAPTURE (half == TimingHandle::attack ? "Attack" : "Release");
        const auto plot = inset.plot (half);
        const auto [from, to] = axisOf (half);
        CHECK_THAT (inset.xAt (half, from), WithinAbs (plot.getX(), 1.0e-3));
        CHECK_THAT (inset.xAt (half, to), WithinAbs (plot.getRight(), 1.0e-3));
        // Each decade the same width.
        const double decade = pixelsPerDecade (inset, half);
        for (double ms = from; ms * 10.0 <= to; ms *= 10.0)
            CHECK_THAT (inset.xAt (half, 10.0 * ms) - inset.xAt (half, ms), WithinAbs (decade, 1.0e-3));
        CHECK_THAT (inset.timeAt (half, inset.xAt (half, 42.0)), WithinRel (42.0, 1.0e-5));
    }
}

TEST_CASE ("The Timing Response inset draws its curves straight between samples, never a step of a sample held across the times between")
{
    TimingHost host;
    auto& inset = host.inset();
    // FET's fastest Attack, whose rise lasts a few samples at 48 kHz.
    REQUIRE (host.processor.chooseStyle (comp1::Style::FET));
    host.edit (parameters::attackId, 0.05f);
    REQUIRE (host.drawsWhatIsHeld());
    for (const auto half : { TimingHandle::attack, TimingHandle::release })
    {
        CAPTURE (half == TimingHandle::attack ? "Attack" : "Release");
        const auto& vertices = inset.curve (half);
        REQUIRE (vertices.size() > 10);
        int moving = 0;
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
        {
            CAPTURE (i, vertices[i].x);
            CHECK (vertices[i].x > vertices[i - 1].x);
            // Each vertex a sample of its own: two in a row never read the same sample while the curve still moves.
            const bool rising = half == TimingHandle::attack;
            if (rising ? vertices[i].y < 0.999 * vertices.back().y : vertices[i].y > 1.0e-3)
            {
                CHECK_FALSE (juce::exactlyEqual (vertices[i].y, vertices[i - 1].y));
                ++moving;
            }
        }
        CHECK (moving > 2);
    }
}

TEST_CASE ("The Timing Response inset's Attack handle sits on the rise at Attack and its Release handle on the recovery at Hold plus Release")
{
    TimingHost host;
    auto& inset = host.inset();
    const auto check = [&] {
        REQUIRE (host.drawsWhatIsHeld());
        host.settle();
        const auto& response = inset.drawn()->response;
        const double attackMs = host.value (parameters::attackId), releaseFromMs = host.value (parameters::holdId) + host.value (parameters::releaseId);
        const auto attack = inset.handleCentre (TimingHandle::attack), release = inset.handleCentre (TimingHandle::release);
        CHECK_THAT (attack.x, WithinAbs (inset.xAt (TimingHandle::attack, attackMs), 1.0e-3));
        CHECK_THAT (attack.y, WithinAbs (inset.yAt (inset.curveAt (TimingHandle::attack, attackMs)), 1.0e-3));
        CHECK_THAT (release.x, WithinAbs (inset.xAt (TimingHandle::release, releaseFromMs), 1.0e-3));
        CHECK_THAT (release.y, WithinAbs (inset.yAt (inset.curveAt (TimingHandle::release, releaseFromMs)), 1.0e-3));
        // On Clean, one pole each: the rise is 1 - 1/e of the way at Attack, the recovery 1/e of the way back at
        // Release once Hold is over, within a sample's step at the axis's coarsest; and the curves drawn are the
        // response's.
        const double most = response.rise.back();
        CHECK_THAT (response.riseAt (attackMs) / most, WithinAbs (1.0 - std::exp (-1.0), 0.02));
        CHECK_THAT (response.recoveryAt (releaseFromMs) / most, WithinAbs (std::exp (-1.0), 0.02));
        CHECK_THAT (inset.curveAt (TimingHandle::attack, attackMs) / most, WithinAbs (1.0 - std::exp (-1.0), 0.02));
        CHECK_THAT (inset.curveAt (TimingHandle::release, releaseFromMs) / most, WithinAbs (std::exp (-1.0), 0.02));
    };
    check();
    host.edit (parameters::attackId, 1.0f);
    host.edit (parameters::releaseId, 40.0f);
    host.edit (parameters::holdId, 20.0f);
    check();

    // Attack's shortest at the axis's start, and an Attack under one sample's time at 48 kHz (about 21 us) right of it,
    // where it lies on the axis.
    host.edit (parameters::attackId, 0.005f);
    REQUIRE (host.drawsWhatIsHeld());
    host.settle();
    CHECK_THAT (inset.handleCentre (TimingHandle::attack).x, WithinAbs (inset.plot (TimingHandle::attack).getX(), 1.0e-3));
    host.edit (parameters::attackId, 0.01f);
    REQUIRE (host.drawsWhatIsHeld());
    host.settle();
    CHECK_THAT (inset.handleCentre (TimingHandle::attack).x - inset.plot (TimingHandle::attack).getX(),
                WithinAbs (std::log10 (2.0) * pixelsPerDecade (inset, TimingHandle::attack), 0.05));
}

TEST_CASE ("A Timing Response inset handle's readout card sits beside it, inside the inset, never over it or the captions and time labels")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.processor.chooseStyle (comp1::Style::FET));
    const auto inside = inset.getLocalBounds().toFloat();
    // Each handle at the start, the middle and the end of its range, so at the top, the middle and the bottom of its
    // curve and at either side.
    for (const auto which : { TimingHandle::attack, TimingHandle::release })
        for (const float ms : which == TimingHandle::attack ? std::vector<float> { 0.005f, 0.5f, 5.0f, 250.0f } : std::vector<float> { 5.0f, 100.0f, 5000.0f })
        {
            CAPTURE (which == TimingHandle::attack ? "Attack" : "Release", ms);
            host.edit (which == TimingHandle::attack ? parameters::attackId : parameters::releaseId, ms);
            REQUIRE (host.drawsWhatIsHeld());
            host.settle();
            auto& handle = inset.handle (which);
            const auto from = centreOf (inset, which);
            const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            handle.mouseDown (harness::mouseEvent (handle, from, held));
            const auto card = inset.readoutBounds();
            REQUIRE (card.has_value());
            const auto disc = juce::Rectangle<float> (2.0f * comp1::TimingResponseInset::handleRadius, 2.0f * comp1::TimingResponseInset::handleRadius)
                                  .withCentre (inset.handleCentre (which));
            CHECK_FALSE (card->intersects (disc));
            CHECK (inside.contains (*card));
            // Level with the plots, so clear of the captions over them and the time labels under them ("10 ms" under
            // FET's quick Attack, at the plot's foot).
            const auto plot = inset.plot (which);
            CHECK (card->getY() >= plot.getY());
            CHECK (card->getBottom() <= plot.getBottom());
            handle.mouseUp (harness::mouseEvent (handle, from, {}, from));
        }
}

TEST_CASE ("Dragging the Timing Response inset's Attack handle moves only Attack and the Release handle only Release, across at the time axis's scale, Shift a quarter as far, each drag one undo step inside gestures")
{
    TimingHost host;
    host.edit (parameters::attackId, 1.0f);
    host.edit (parameters::releaseId, 100.0f);
    host.edit (parameters::holdId, 20.0f);
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    host.settle();
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    const double attackDecade = pixelsPerDecade (inset, TimingHandle::attack), releaseDecade = pixelsPerDecade (inset, TimingHandle::release);

    host::GestureRecorder recorder;
    host.processor.addListener (&recorder);
    // The Attack handle right (and down, which changes nothing): Attack up by the decades the pixels span.
    auto before = allValues (host);
    dragHandle (inset, TimingHandle::attack, { 30.0f, 25.0f });
    CHECK_THAT (host.value (parameters::attackId), WithinRel (1.0 * std::pow (10.0, 30.0 / attackDecade), 1.0e-3));
    auto after = allValues (host);
    after.erase (parameters::attackId);
    before.erase (parameters::attackId);
    CHECK (after == before);
    CHECK (history.undoSteps() == start + 1);

    // The Release handle left, from Hold plus Release: Release is the time there less Hold.
    before = allValues (host);
    dragHandle (inset, TimingHandle::release, { -10.0f, 0.0f });
    CHECK_THAT (host.value (parameters::releaseId), WithinRel (120.0 * std::pow (10.0, -10.0 / releaseDecade) - 20.0, 1.0e-3));
    after = allValues (host);
    after.erase (parameters::releaseId);
    before.erase (parameters::releaseId);
    CHECK (after == before);
    CHECK (history.undoSteps() == start + 2);

    // Shift: a quarter as far.
    host.settle();
    const double attackNow = host.value (parameters::attackId);
    dragHandle (inset, TimingHandle::attack, { -40.0f, 0.0f }, juce::ModifierKeys::shiftModifier);
    CHECK_THAT (host.value (parameters::attackId), WithinRel (attackNow * std::pow (10.0, -10.0 / attackDecade), 1.0e-3));
    CHECK (history.undoSteps() == start + 3);
    host.processor.removeListener (&recorder);

    REQUIRE_FALSE (recorder.changes.empty());
    for (const auto& [index, inGesture] : recorder.changes)
    {
        CAPTURE (index);
        CHECK (inGesture);
    }
    CHECK (recorder.open.empty());

    for (int i = 0; i < 3; ++i)
        history.undo();
    CHECK_THAT (host.value (parameters::attackId), WithinRel (1.0f, 1.0e-4f));
    CHECK_THAT (host.value (parameters::releaseId), WithinRel (100.0f, 1.0e-4f));
}

TEST_CASE ("A Timing Response inset handle stops at its setting's range")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    dragHandle (inset, TimingHandle::attack, { -500.0f, 0.0f });
    CHECK_THAT (host.value (parameters::attackId), WithinRel (0.005f, 1.0e-3f));
    dragHandle (inset, TimingHandle::release, { 500.0f, 0.0f });
    CHECK_THAT (host.value (parameters::releaseId), WithinRel (5000.0f, 1.0e-3f));
}

TEST_CASE ("Only the Timing Response inset's handles take a press: elsewhere in the inset a press passes through")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    host.settle();
    auto& display = host.display();
    const auto pressedAt = [&] (juce::Point<float> inInset) {
        return display.getComponentAt (display.getLocalPoint (&inset, inInset).roundToInt());
    };
    for (const auto which : { TimingHandle::attack, TimingHandle::release })
        CHECK (pressedAt (inset.handleCentre (which)) == &inset.handle (which));
    int checked = 0;
    for (float y = 2.0f; y < static_cast<float> (inset.getHeight()); y += 6.0f)
        for (float x = 2.0f; x < static_cast<float> (inset.getWidth()); x += 6.0f)
        {
            const juce::Point<float> at { x, y };
            if (at.getDistanceFrom (inset.handleCentre (TimingHandle::attack)) <= comp1::TimingResponseInset::handleHitRadius + 1.5f
                || at.getDistanceFrom (inset.handleCentre (TimingHandle::release)) <= comp1::TimingResponseInset::handleHitRadius + 1.5f)
                continue;
            CAPTURE (x, y);
            auto* target = pressedAt (at);
            CHECK (target != &inset);
            CHECK_FALSE (inset.isParentOf (target));
            ++checked;
        }
    CHECK (checked > 500);
}

TEST_CASE ("A Timing Response inset handle lights while hovered, and while dragged is selected with a readout card of its setting")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    auto& attack = inset.handle (TimingHandle::attack);
    CHECK (attack.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor));
    CHECK_FALSE (inset.lookOf (TimingHandle::attack).hovered);
    attack.mouseEnter (harness::mouseEvent (attack, centreOf (inset, TimingHandle::attack)));
    CHECK (inset.lookOf (TimingHandle::attack).hovered);
    attack.mouseExit (harness::mouseEvent (attack, centreOf (inset, TimingHandle::attack)));
    CHECK_FALSE (inset.lookOf (TimingHandle::attack).hovered);

    CHECK_FALSE (inset.readout().has_value());
    auto& release = inset.handle (TimingHandle::release);
    const auto from = centreOf (inset, TimingHandle::release);
    const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    release.mouseDown (harness::mouseEvent (release, from, held));
    CHECK (inset.lookOf (TimingHandle::release).selected);
    CHECK_FALSE (inset.lookOf (TimingHandle::attack).selected);
    REQUIRE (inset.readout().has_value());
    CHECK (inset.readout()->title == "Release");
    CHECK (inset.readout()->value == "100.0 ms");
    CHECK (inset.readout()->at == TimingHandle::release);
    release.mouseUp (harness::mouseEvent (release, from, {}, from));
    CHECK_FALSE (inset.readout().has_value());
    CHECK_FALSE (inset.lookOf (TimingHandle::release).selected);
}

TEST_CASE ("Arrows on the focused Timing Response inset step Attack (left and right) and Release (up and down) by 1% of their travel, 0.2% with Shift, a held key one undo step")
{
    TimingHost host;
    auto& inset = host.inset();
    inset.grabKeyboardFocus();
    REQUIRE (inset.hasKeyboardFocus (false));
    auto& history = host.processor.session().editHistory();
    const int start = history.undoSteps();
    const auto travel = [&host] (const juce::String& id) { return host.parameter (id).getValue(); };
    const float attack = travel (parameters::attackId), release = travel (parameters::releaseId);
    auto before = allValues (host);

    CHECK (host.press (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK_THAT (travel (parameters::attackId), WithinAbs (attack + 0.01f, 1.0e-6));
    CHECK (host.press (harness::withShift (juce::KeyPress (juce::KeyPress::leftKey))));
    CHECK_THAT (travel (parameters::attackId), WithinAbs (attack + 0.008f, 1.0e-6));
    CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK_THAT (travel (parameters::releaseId), WithinAbs (release + 0.01f, 1.0e-6));
    CHECK (host.press (harness::withShift (juce::KeyPress (juce::KeyPress::downKey))));
    CHECK_THAT (travel (parameters::releaseId), WithinAbs (release + 0.008f, 1.0e-6));
    CHECK (history.undoSteps() == start + 4);
    auto after = allValues (host);
    for (auto* values : { &after, &before })
    {
        values->erase (parameters::attackId);
        values->erase (parameters::releaseId);
    }
    CHECK (after == before);

    REQUIRE (host.hold (juce::KeyPress (juce::KeyPress::rightKey)));
    REQUIRE (host.hold (juce::KeyPress (juce::KeyPress::rightKey)));
    host.release();
    CHECK_THAT (travel (parameters::attackId), WithinAbs (attack + 0.028f, 1.0e-6));
    CHECK (history.undoSteps() == start + 5);
}

TEST_CASE ("The Timing Response inset is a slider titled Timing Response inset, read with Attack and Release as their knobs read them, a Tab stop after the Transfer Curve inset; its handles aren't other elements")
{
    TimingHost host;
    auto& inset = host.inset();
    host.settle();
    auto* handler = inset.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::slider);
    CHECK (handler->getTitle() == "Timing Response inset");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "Attack 10.000 ms, Release 100.0 ms");
    CHECK (inset.getWantsKeyboardFocus());
    for (const auto which : { TimingHandle::attack, TimingHandle::release })
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
    const int curveInset = titles.indexOf ("Transfer Curve inset");
    REQUIRE (curveInset >= 0);
    CHECK (titles[curveInset + 1] == "Timing Response inset");
}

TEST_CASE ("Under Global Bypass the Timing Response inset dims its curves and handles to 45 % of their alphas, and its handles still drag")
{
    TimingHost host;
    auto& inset = host.inset();
    REQUIRE (host.drawsWhatIsHeld());
    CHECK_THAT (inset.curveAlpha(), WithinAbs (1.0, 1.0e-6));
    for (const auto which : { TimingHandle::attack, TimingHandle::release })
        CHECK_THAT (inset.lookOf (which).alpha, WithinAbs (1.0, 1.0e-6));

    host.edit (parameters::globalBypassId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (inset.curveAlpha(), 0.45f); }));
    for (const auto which : { TimingHandle::attack, TimingHandle::release })
        CHECK_THAT (inset.lookOf (which).alpha, WithinAbs (0.45, 1.0e-6));

    const float before = host.value (parameters::releaseId);
    dragHandle (inset, TimingHandle::release, { 12.0f, 0.0f });
    CHECK (host.value (parameters::releaseId) > before);
    host.edit (parameters::globalBypassId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (inset.curveAlpha(), 1.0f); }));
}

TEST_CASE ("The Timing Response inset draws Auto Release's recovery as a band from Release to 5 x Release with the Release handle on its fast edge, and on Opto the rise over a band from 0.5 x to 2 x Attack and the slow part's times shaded as fixed")
{
    TimingHost host;
    auto& inset = host.inset();
    // Each vertex drawn at one of the response's times, with the value worked out there.
    const auto drawsValues = [] (const std::vector<juce::Point<double>>& vertices, const std::array<double, comp1::TimingResponse::points>& values) {
        REQUIRE (vertices.size() > 10);
        for (const auto& v : vertices)
        {
            const auto i = static_cast<std::size_t> (std::lround (std::log10 (v.x / comp1::TimingResponse::earliestMs) * comp1::TimingResponse::pointsPerDecade));
            CHECK (juce::exactlyEqual (v.y, values[i]));
        }
    };
    for (const auto style : { comp1::Style::Clean, comp1::Style::VCA, comp1::Style::FET, comp1::Style::Opto })
        for (const bool autoRelease : { false, true })
        {
            REQUIRE (host.processor.chooseStyle (style));
            host.edit (parameters::autoReleaseId, autoRelease ? 1.0f : 0.0f);
            host.edit (parameters::holdId, 25.0f);
            const bool opto = style == comp1::Style::Opto;
            CAPTURE (static_cast<int> (style), autoRelease);
            REQUIRE (host.drawsWhatIsHeld());
            host.settle();
            const auto& response = inset.drawn()->response;

            REQUIRE (inset.band (TimingHandle::release).has_value() == autoRelease);
            if (autoRelease)
            {
                const auto& band = *inset.band (TimingHandle::release);
                drawsValues (band.fastest, response.recoveryBand->fastest);
                drawsValues (band.slowest, response.recoveryBand->slowest);
                // The band's fast edge is the recovery drawn, and the Release handle sits on it.
                CHECK (inset.curve (TimingHandle::release) == band.fastest);
                const double releaseFromMs = host.value (parameters::holdId) + host.value (parameters::releaseId);
                CHECK_THAT (inset.handleCentre (TimingHandle::release).y, WithinAbs (inset.yAt (inset.curveAt (TimingHandle::release, releaseFromMs)), 1.0e-3));
            }
            else
                drawsValues (inset.curve (TimingHandle::release), response.recovery);

            REQUIRE (inset.band (TimingHandle::attack).has_value() == opto);
            drawsValues (inset.curve (TimingHandle::attack), response.rise);
            if (opto)
            {
                drawsValues (inset.band (TimingHandle::attack)->fastest, response.riseBand->fastest);
                drawsValues (inset.band (TimingHandle::attack)->slowest, response.riseBand->slowest);
            }

            REQUIRE (inset.slowPartMs().has_value() == opto);
            if (opto)
            {
                CHECK_THAT (inset.slowPartMs()->getStart(), WithinAbs (25.0 + 500.0, 1.0e-3));
                CHECK_THAT (inset.slowPartMs()->getEnd(), WithinAbs (25.0 + 5000.0, 1.0e-3));
            }
        }
}

TEST_CASE ("Destroyed while it works a response out, the Timing Response worker stops it rather than waiting for it to finish")
{
    using clock = std::chrono::steady_clock;
    // The slowest response to work out: Opto, Auto Release, Release's longest.
    const comp1::Settings slowest { .character = comp1::Character::Opto, .releaseMs = 5000.0, .autoRelease = true };
    const auto start = clock::now();
    const auto response = comp1::timingResponse (slowest, comp1::drawnSampleRate);
    const auto workingOut = clock::now() - start;
    REQUIRE (response.rise.back() > 0.0);

    std::optional<comp1::TimingResponseWorker> worker;
    worker.emplace();
    REQUIRE (worker->ask ({ slowest }));
    // Let the worker take the request and start on it.
    std::this_thread::sleep_for (workingOut / 8);
    const auto destroying = clock::now();
    worker.reset();
    const auto stopping = clock::now() - destroying;
    const double workingOutMs = std::chrono::duration<double, std::milli> (workingOut).count();
    const double stoppingMs = std::chrono::duration<double, std::milli> (stopping).count();
    CAPTURE (workingOutMs, stoppingMs);
    CHECK (stopping < workingOut / 2);
}
