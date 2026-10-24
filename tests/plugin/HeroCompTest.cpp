// The Compact View's Comp beyond the Expanded View's (HeroComp, ADR 0011): its Detection Level ring, placed around the
// Threshold the compressor plays, and its Comp Follow-off cue, a dimmed arc and a tooltip.

#include "Playing.h"
#include "comp1/Style.h"
#include "plugkit/ui/staple/TooltipWindow.h"
#include "plugkit/ui/staple/controls/KnobTooltip.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <utility>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

void showCompactView (host::EditorHost& host)
{
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.expandedView().compactView().isShowing());
}

void showExpandedView (host::EditorHost& host)
{
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE_FALSE (host.expandedView().compactView().isShowing());
}

comp1::HeroComp& hero (host::EditorHost& host) { return host.expandedView().compactView().heroComp(); }

// The Compact View's Comp knob.
staple::Knob& heroKnob (host::EditorHost& host)
{
    auto& knob = host.showing<staple::Knob> ("Comp");
    REQUIRE (host.expandedView().compactView().isParentOf (&knob));
    return knob;
}

// Where the knob's pointer is on its sweep, 0 to 1.
double pointerOf (staple::Knob& knob) { return knob.valueToProportionOfLength (knob.getValue()); }

// The arc at its full alpha, or dimmed to the Global Bypass scale.
bool arcDimmed (staple::Knob& knob)
{
    const float alpha = knob.getArcColour().getFloatAlpha();
    if (std::abs (alpha - 1.0f) <= 1.0e-6f)
        return false;
    CHECK_THAT (alpha, WithinAbs (staple::tokens::motion::globalBypassScale, 1.0 / 255.0));
    return true;
}

} // namespace

TEST_CASE ("The Compact View's Comp has a Detection Level ring in Staple's lane round the hero's face, display only, and the Expanded View's Comp has none")
{
    host::EditorHost host;
    CHECK_FALSE (host.knob ("Comp").isOnRing (host.knob ("Comp").getFaceCentre().translated (0.0f, -(host.knob ("Comp").getFaceRadius() + staple::tokens::knob::ringOffset))));
    showCompactView (host);
    auto& knob = heroKnob (host);
    const auto at = [&knob] (float out) { return knob.getFaceCentre().translated (0.0f, -(knob.getFaceRadius() + out)); };
    const auto onLane = at (staple::tokens::knob::ringOffset);
    CHECK (knob.isOnRing (onLane));
    // The kit's 12 px lane at 10 px out, unscaled: on it just inside its outer edge, off it just past.
    const float outer = staple::tokens::knob::ringOffset + staple::tokens::knob::ringLane / 2.0f;
    CHECK (knob.isOnRing (at (outer - 1.0f)));
    CHECK_FALSE (knob.isOnRing (at (outer + 1.0f)));
    // Its label under the lane.
    auto* label = knob.getLabel();
    REQUIRE (label != nullptr);
    CHECK (label->getY() >= knob.getY() + juce::roundToInt (knob.getFaceCentre().y + knob.getFaceRadius() + outer) - 1);

    // Display only: a press on the lane leaves Comp where it was, and its tooltip lines are empty.
    const float comp = host.value (parameters::compId);
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    knob.mouseDown (harness::mouseEvent (knob, onLane, mods));
    knob.mouseDrag (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), mods, onLane));
    knob.mouseUp (harness::mouseEvent (knob, onLane.translated (0.0f, -40.0f), {}, onLane));
    CHECK (host.value (parameters::compId) == comp);
    CHECK (hero (host).detectionLevelRing().ringTitle().isEmpty());
    CHECK (hero (host).detectionLevelRing().ringValue().isEmpty());
}

TEST_CASE ("The Compact View's Detection Level ring follows the Detection Level as Threshold's does, under Global Bypass and Detection Audition too, and falls at 20 dB/s after")
{
    host::Playing host;
    showCompactView (host);
    auto& ring = hero (host).detectionLevelRing();
    CHECK (ring.shownDb() == -60.0);

    const auto how = GENERATE (as<std::string> {}, "plain", "Global Bypass", "Detection Audition");
    CAPTURE (how);
    if (how == "Global Bypass")
        host.edit (parameters::globalBypassId, 1.0f);
    else if (how == "Detection Audition")
        host.processor.setDetectionAudition (true);

    const auto tone = host::sine (std::pow (10.0, -12.0 / 20.0));
    host.playFor (0.3, tone);
    CHECK (host.keepPlayingUntil (tone, [&] { return std::abs (ring.shownDb() + 12.0) <= 0.5; }));
    CHECK (ring.shownDb() == host.ring().shownDb());
    host::checkFallsAt20DbPerSecond (host, [&] { return ring.shownDb(); }, -60.0);
    CHECK (ring.shownDb() == host.ring().shownDb());
    host.processor.setDetectionAudition (false);
}

TEST_CASE ("The Compact View's Detection Level ring reaches Comp's pointer as the Detection Level reaches the Threshold played, with Comp Follow off or on")
{
    host::Playing host;
    host.edit (parameters::kneeId, 0.0f);
    showCompactView (host);
    auto& knob = heroKnob (host);
    auto& ring = hero (host).detectionLevelRing();
    const bool following = GENERATE (false, true);
    CAPTURE (following);
    if (following)
        host.edit (parameters::compFollowId, 1.0f);
    else
        host.edit (parameters::thresholdId, -30.0f);
    const double threshold = host.processor.playedCurve().thresholdDb;
    REQUIRE (threshold > -60.0);
    REQUIRE (threshold < 0.0);
    host.settle (4);
    const double pointer = pointerOf (knob);
    REQUIRE (pointer > 0.0);
    REQUIRE (pointer < 1.0);

    const auto reach = [&] (double levelDb) {
        const auto level = host::steady (levelDb);
        host.playFor (0.2, level);
        REQUIRE (host.keepPlayingUntil (level, [&] { return std::abs (ring.shownDb() - levelDb) <= 0.05; }));
        return ring.filledTo();
    };
    // At the Threshold, on Comp's pointer; under it, short of it in proportion; over it, past it toward the top.
    CHECK_THAT (reach (threshold), WithinAbs (pointer, 0.01));
    CHECK_THAT (reach ((threshold - 60.0) / 2.0), WithinAbs (pointer / 2.0, 0.01));
    CHECK_THAT (reach (threshold / 2.0), WithinAbs ((pointer + 1.0) / 2.0, 0.01));
    CHECK (ring.filledFrom() == 0.0);
}

TEST_CASE ("On a steady level the Compact View's Detection Level ring moves with a Threshold set elsewhere; silent, a Threshold change repaints nothing")
{
    host::Playing host;
    host.edit (parameters::thresholdId, -30.0f);
    showCompactView (host);
    auto& knob = heroKnob (host);
    auto& ring = hero (host).detectionLevelRing();

    // Silent: the ring at the floor, and nothing to repaint as Threshold moves.
    host.playFor (0.2, host::silence());
    auto& repaints = harness::countRepaintsOf (knob);
    host.edit (parameters::thresholdId, -18.0f);
    host.playFor (0.2, host::silence());
    CHECK (ring.shownDb() == -60.0);
    CHECK (repaints.count == 0);
    knob.setCachedComponentImage (nullptr);

    // A steady -24 dB, over a -30 dB Threshold: past Comp's pointer.
    host.edit (parameters::thresholdId, -30.0f);
    const auto level = host::steady (-24.0);
    host.playFor (0.3, level);
    REQUIRE (host.keepPlayingUntil (level, [&] { return std::abs (ring.shownDb() + 24.0) <= 0.05; }));
    CHECK (ring.filledTo() > pointerOf (knob));

    // Threshold over the level, as Host Automation sets it: short of Comp's pointer, with Comp where it was.
    const double comp = knob.getValue();
    host.edit (parameters::thresholdId, -18.0f);
    REQUIRE (host.keepPlayingUntil (level, [&] { return std::abs (ring.shownDb() + 24.0) <= 0.05; }));
    CHECK (ring.filledTo() < pointerOf (knob));
    CHECK (knob.getValue() == comp);
}

TEST_CASE ("The Compact View's Comp arc dims to the Global Bypass scale, with the tooltip, exactly while Comp Follow is off, through every path that changes it")
{
    host::EditorHost host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    showCompactView (host);
    auto& knob = heroKnob (host);
    const auto bounds = knob.getBounds();
    const auto& tooltip = comp1::HeroComp::offTooltip;
    CHECK (tooltip == "Comp Follow off: grab Comp to follow");
    const auto shows = [&] (bool off) {
        REQUIRE (host.settleUntil ([&] { return arcDimmed (knob) == off; }));
        CHECK (knob.getTooltip() == (off ? tooltip : juce::String()));
        CHECK (knob.getBounds() == bounds);
        CHECK (knob.getTitle() == "Comp");
    };
    shows (false);
    auto& history = host.processor.session().editHistory();

    // Host Automation, both ways.
    host.edit (parameters::compFollowId, 0.0f);
    shows (true);
    host.edit (parameters::compFollowId, 1.0f);
    shows (false);

    // A Threshold edit in the Expanded View turns it off; the Expanded View's Comp arc stays full.
    showExpandedView (host);
    host::EditorHost::drag (host.knob ("Threshold"), 10.0f);
    REQUIRE (host.value (parameters::compFollowId) == 0.0f);
    REQUIRE (host.settleUntil ([&] { return arcDimmed (knob); }));
    CHECK_FALSE (arcDimmed (host.knob ("Comp")));
    CHECK (host.knob ("Comp").getTooltip().isEmpty());
    showCompactView (host);
    shows (true);

    // Grabbing Comp turns it on, undo off again, redo on.
    host::EditorHost::drag (knob, 0.0f);
    REQUIRE (host.value (parameters::compFollowId) == 1.0f);
    shows (false);
    history.undo();
    REQUIRE (host.value (parameters::compFollowId) == 0.0f);
    shows (true);
    history.redo();
    shows (false);

    // A Style's defaults turn it on.
    host.edit (parameters::compFollowId, 0.0f);
    shows (true);
    REQUIRE (host.processor.chooseStyle (comp1::Style::Bus));
    shows (false);
}

namespace
{

// The pointer resting on the hero Comp's face with Comp Follow off: its Knob Readout shows at once, and after the rest
// the editor's Tooltip window shows the cue's tooltip, placed by the knob as JUCE's timer would show it (a test runs in
// the background, where JUCE's getTipFor answers nothing). The two cards, in the editor's coordinates.
std::pair<juce::Rectangle<int>, juce::Rectangle<int>> restOnHero (host::EditorHost& host)
{
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compFollowId, 0.0f);
    showCompactView (host);
    auto& knob = heroKnob (host);
    REQUIRE (host.settleUntil ([&] { return knob.getTooltip() == comp1::HeroComp::offTooltip; }));
    const auto centre = knob.getFaceCentre();
    host.settleAnimations();
    // Nothing settles from here: the cards' watchdogs would read the machine's real mouse, off a test's window, and
    // hide them. The Readout shown at the end of its fade-in.
    knob.mouseEnter (harness::mouseEvent (knob, centre, {}));
    auto* readout = knob.getKnobTooltip();
    REQUIRE (readout != nullptr);
    REQUIRE (readout->isVisible());
    readout->setAlpha (1.0f);

    auto* tips = harness::findChild<staple::TooltipWindow> (*host.editor);
    REQUIRE (tips != nullptr);
    tips->getTipFor (knob);
    tips->displayTip (knob.localPointToGlobal (centre).roundToInt(), knob.getTooltip());
    REQUIRE (tips->isVisible());
    return { host.editor->getLocalArea (tips, tips->getLocalBounds()), host.editor->getLocalArea (readout, readout->getLocalBounds()) };
}

} // namespace

TEST_CASE ("With Comp Follow off, the hero Comp's tooltip after a rest on its face doesn't cover its Knob Readout")
{
    host::EditorHost host;
    const auto [tip, readout] = restOnHero (host);
    CAPTURE (tip.toString(), readout.toString());
    CHECK_FALSE (tip.intersects (readout));
    CHECK (host.editor->getLocalBounds().contains (tip));
    // Beside the knob, short of its lane.
    auto& knob = heroKnob (host);
    const auto face = knob.getFaceCentre();
    const float lane = knob.getFaceRadius() + staple::tokens::knob::ringOffset + staple::tokens::knob::ringLane / 2.0f;
    CHECK (static_cast<float> (tip.getRight()) <= host.editor->getLocalPoint (&knob, face).x - lane);
}

TEST_CASE ("Compact View snapshot: the hero Comp hovered with Comp Follow off, its Knob Readout and its tooltip", "[.screens]")
{
    host::EditorHost host;
    restOnHero (host);
    harness::writeSnapshot (*host.editor, "compact-comp-follow-off-hover", {}, 1.0f);
}
