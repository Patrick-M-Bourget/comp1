// The Expanded View's Transfer Curve Graph, the Transfer Curve tab: the curve it draws from the settings the Engine
// plays, and the Detection Level's dot on it, read through its test model (TransferCurveGraph::outputAt, curve,
// dotDb) rather than its pixels.

#include "Playing.h"
#include "TransferCurveGraph.h"
#include "comp1/Style.h"
#include "comp1/TransferCurve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace parameters = comp1::parameters;

namespace
{

constexpr float infinity = std::numeric_limits<float>::infinity();

struct Graphing : host::Playing
{
    using Playing::Playing;

    comp1::TransferCurveGraph& graph() { return expandedView().display().transferCurveGraph(); }

    // Waits for the graph to draw from thresholdDb, ratio, kneeDb and rangeDb (to the float a host parameter holds).
    bool drawsFrom (double thresholdDb, double ratio, double kneeDb, double rangeDb)
    {
        return settleUntil ([&] { return isDrawingFrom (thresholdDb, ratio, kneeDb, rangeDb); });
    }

    bool isDrawingFrom (double thresholdDb, double ratio, double kneeDb, double rangeDb)
    {
        const auto near = [] (double drawn, double expected) { return std::abs (drawn - expected) <= 1.0e-5 * std::max (1.0, std::abs (expected)); };
        const auto curve = graph().curve();
        return near (curve.thresholdDb, thresholdDb) && near (curve.inverseRatio, 1.0 / ratio) && near (curve.kneeDb, kneeDb)
               && near (curve.rangeDb, rangeDb);
    }

    // What the graph should draw from now: the processor's Threshold and Ratio while Comp Follow is on, else the stored
    // ones; the stored Knee and Range.
    bool drawsWhatPlays()
    {
        return settleUntil ([&] {
            const bool following = value (parameters::compFollowId) > 0.5f;
            return isDrawingFrom (following ? processor.followedThresholdDb() : value (parameters::thresholdId),
                                  following ? processor.followedRatio() : value (parameters::ratioId),
                                  value (parameters::kneeId),
                                  value (parameters::rangeId));
        });
    }

    void setCurve (float thresholdDb, float ratio, float kneeDb, float rangeDb)
    {
        edit (parameters::thresholdId, thresholdDb);
        edit (parameters::ratioId, ratio);
        edit (parameters::kneeId, kneeDb);
        edit (parameters::rangeId, rangeDb);
    }
};

// What a screen reader reads as the graph's value.
juce::String spoken (juce::Component& c) { return c.getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString(); }

const juce::String minus { juce::CharPointer_UTF8 ("\xe2\x88\x92") };

} // namespace

TEST_CASE ("The Transfer Curve Graph draws output as input less the Transfer Curve's Gain Reduction, for every Threshold, Ratio, Knee and Range", "[sweep]")
{
    Graphing host;
    for (const float threshold : { -60.0f, -30.0f, -10.0f })
        for (const float ratio : { 1.0f, 2.0f, 4.0f, infinity })
            for (const float knee : { 0.0f, 12.0f, 72.0f })
                for (const float range : { 60.0f, 10.0f })
                {
                    CAPTURE (threshold, ratio, knee, range);
                    host.setCurve (threshold, ratio, knee, range);
                    REQUIRE (host.drawsFrom (host.value (parameters::thresholdId), host.value (parameters::ratioId), knee, range));
                    // The Transfer Curve the stored values set, as the Engine's public function reads them.
                    const comp1::clean::Curve played { .thresholdDb = host.value (parameters::thresholdId),
                                                       .inverseRatio = 1.0 / host.value (parameters::ratioId),
                                                       .kneeDb = knee,
                                                       .rangeDb = range };
                    for (int input = -60; input <= 0; ++input)
                    {
                        CAPTURE (input);
                        CHECK (host.graph().outputAt (input) == input - comp1::clean::gainReduction (input, played));
                    }
                }
}

TEST_CASE ("The Transfer Curve Graph's curve at two hand-checked points: hard and soft knee at -30 dB and 4:1")
{
    Graphing host;
    // 24 dB over Threshold, of which 4:1 takes off 18.
    host.setCurve (-30.0f, 4.0f, 0.0f, 60.0f);
    REQUIRE (host.drawsFrom (-30.0, host.value (parameters::ratioId), 0.0, 60.0));
    CHECK_THAT (host.graph().outputAt (-6.0), WithinAbs (-24.0, 1.0e-4));
    // At Threshold in the middle of a 12 dB Knee: 3/4 of (6 dB)^2 / 24 dB = 1.125 dB off.
    host.edit (parameters::kneeId, 12.0f);
    REQUIRE (host.drawsFrom (-30.0, host.value (parameters::ratioId), 12.0, 60.0));
    CHECK_THAT (host.graph().outputAt (-30.0), WithinAbs (-31.125, 1.0e-4));
}

TEST_CASE ("The Transfer Curve Graph's curve is what the processor plays on a steady level")
{
    Graphing host;
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::outputGainId, 0.0f);
    host.edit (parameters::attackId, 1.0f);
    host.edit (parameters::releaseId, 50.0f);
    host.setCurve (-30.0f, 4.0f, 12.0f, 60.0f);
    REQUIRE (host.drawsWhatPlays());

    for (const double input : { -45.0, -32.0, -24.0, -12.0, -3.0 })
    {
        CAPTURE (input);
        // Long enough for Attack and Release to settle, then the peak of one block.
        const auto buffer = host.play (200, host::Playing::blockSize, host::steady (input));
        const double outputDb = juce::Decibels::gainToDecibels (static_cast<double> (buffer.getMagnitude (0, 0, buffer.getNumSamples())));
        CHECK_THAT (outputDb, WithinAbs (host.graph().outputAt (input), 0.05));
    }
}

TEST_CASE ("While Comp Follow is on the Transfer Curve Graph draws the followed Threshold and Ratio, moving with Comp and not with the stored ones; grabbing Threshold keeps the curve, then follows the drag")
{
    Graphing host;
    // Opto at Comp 80%: the Comp Curve's -32.4 dB and 4.29:1, with Opto's 12 dB Knee.
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    host.edit (parameters::compId, 80.0f);
    REQUIRE (host.drawsFrom (host.processor.followedThresholdDb(), host.processor.followedRatio(), 12.0, 60.0));
    CHECK_THAT (host.graph().curve().thresholdDb, WithinAbs (-32.4, 1.0e-3));

    // Host Automation of the stored Threshold and Ratio: nothing drawn changes.
    const auto before = host.graph().curve();
    host.set (parameters::thresholdId, -6.0f);
    host.set (parameters::ratioId, 1.5f);
    host.settle();
    CHECK (host.graph().curve().thresholdDb == before.thresholdDb);
    CHECK (host.graph().curve().inverseRatio == before.inverseRatio);

    // A Comp drag moves it.
    host::EditorHost::drag (host.knob ("Comp"), 20.0f);
    REQUIRE (host.value (parameters::compId) > 80.0f);
    REQUIRE (host.drawsFrom (host.processor.followedThresholdDb(), host.processor.followedRatio(), 12.0, 60.0));
    CHECK (host.graph().curve().thresholdDb < before.thresholdDb);

    // Grabbing Threshold turns Comp Follow off at the followed values: the curve stays where it was.
    const auto followed = host.graph().curve();
    auto& threshold = host.knob ("Threshold");
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    const auto centre = threshold.getFaceCentre();
    threshold.mouseDown (harness::mouseEvent (threshold, centre, mods));
    REQUIRE (host.value (parameters::compFollowId) == 0.0f);
    host.settle();
    CHECK_THAT (host.graph().curve().thresholdDb, WithinAbs (followed.thresholdDb, 1.0e-4));
    CHECK_THAT (host.graph().curve().inverseRatio, WithinAbs (followed.inverseRatio, 1.0e-4));
    // Then follows the drag.
    threshold.mouseDrag (harness::mouseEvent (threshold, centre.translated (0.0f, -20.0f), mods, centre));
    threshold.mouseUp (harness::mouseEvent (threshold, centre.translated (0.0f, -20.0f), {}, centre));
    REQUIRE (host.value (parameters::thresholdId) > followed.thresholdDb + 0.5);
    CHECK (host.drawsFrom (host.value (parameters::thresholdId), host.value (parameters::ratioId), 12.0, 60.0));
}

TEST_CASE ("The Transfer Curve Graph draws each Style's curve, the previous one on undo, and an A/B side's and a restored session's")
{
    Graphing host;
    for (int s = 0; s < comp1::styleCount; ++s)
    {
        const auto style = static_cast<comp1::Style> (s);
        CAPTURE (s);
        REQUIRE (host.processor.chooseStyle (style));
        // Each Style lands its default Comp on its default Threshold and Ratio, with its Knee and Range.
        const auto& defaults = *comp1::describe (style).defaults;
        CHECK (host.drawsFrom (defaults.thresholdDb, defaults.ratio, defaults.kneeDb, defaults.rangeDb));
    }

    // Undo: the Style before.
    auto& history = host.processor.session().editHistory();
    REQUIRE (host.processor.chooseStyle (comp1::Style::Opto));
    REQUIRE (host.drawsFrom (-18.0, 3.0, 12.0, 60.0));
    history.undo();
    CHECK (host.drawsWhatPlays());
    CHECK (host.graph().curve().kneeDb == comp1::describe (comp1::Style::Drums).defaults->kneeDb);

    // A/B: B with its own curve, then A again.
    auto& session = host.processor.session();
    host.edit (parameters::compFollowId, 0.0f);
    host.setCurve (-40.0f, 8.0f, 0.0f, 20.0f);
    REQUIRE (host.drawsWhatPlays());
    const auto onA = host.graph().curve();
    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    host.setCurve (-12.0f, 2.0f, 24.0f, 60.0f);
    REQUIRE (host.drawsWhatPlays());
    session.selectCompareSide (plugkit::ui::CompareSide::A);
    CHECK (host.drawsFrom (onA.thresholdDb, 1.0 / onA.inverseRatio, onA.kneeDb, onA.rangeDb));

    // A restored session, into the open editor.
    const auto saved = host::saved (host.processor);
    host.setCurve (-50.0f, 1.5f, 6.0f, 30.0f);
    REQUIRE (host.drawsWhatPlays());
    host::restore (host.processor, saved);
    CHECK (host.drawsFrom (onA.thresholdDb, 1.0 / onA.inverseRatio, onA.kneeDb, onA.rangeDb));
}

TEST_CASE ("The Transfer Curve Graph's dot sits on the curve at the Detection Level, falls at 20 dB/s when the tone stops, then isn't drawn")
{
    Graphing host;
    host.setCurve (-30.0f, 4.0f, 6.0f, 60.0f);
    REQUIRE (host.drawsWhatPlays());
    CHECK_FALSE (host.graph().dotDb().has_value());

    const auto tone = host::steady (-12.0);
    CHECK (host.keepPlayingUntil (tone, [&] { return host.graph().dotDb() && std::abs (*host.graph().dotDb() + 12.0) <= 0.5; }));
    // On the curve: the output it draws at the Detection Level.
    const auto dot = host.graph().dot();
    REQUIRE (dot.has_value());
    CHECK (juce::exactlyEqual (dot->y, host.graph().outputAt (dot->x)));

    // Falls at 20 dB/s, still drawn: from -12 dB it falls well short of the bottom of the scale in the wait.
    constexpr double bottom = -comp1::TransferCurveGraph::scaleDb;
    host::checkFallsAt20DbPerSecond (host, [&] { return host.graph().dotDb().value_or (bottom); }, bottom);
    REQUIRE (host.graph().dotDb().has_value());
    // Then gone, at or under the bottom of the scale.
    CHECK (host.keepPlayingUntil (host::silence(), [&] { return ! host.graph().dotDb().has_value(); }));
}

TEST_CASE ("With Detection Source External and no Sidechain, the Transfer Curve Graph draws no dot")
{
    Graphing host;
    host.edit (parameters::detectionSourceId, 1.0f);
    host.playFor (0.3, host::steady (-6.0));
    CHECK_FALSE (host.graph().dotDb().has_value());
}

TEST_CASE ("With no audio and no edits the Transfer Curve Graph asks for no repaint; an edit to Knee repaints it")
{
    Graphing host;
    host.processor.setDisplayTab (comp1::DisplayTab::transferCurve);
    REQUIRE (host.settleUntil ([&] { return host.graph().isShowing(); }));
    host.playFor (0.2, host::silence());
    auto& repaints = harness::countRepaintsOf (host.graph());
    host.settle (300);
    CHECK (repaints.count == 0);

    host.edit (parameters::kneeId, 30.0f);
    CHECK (host.settleUntil ([&] { return repaints.count > 0; }));
    host.graph().setCachedComponentImage (nullptr);
}

TEST_CASE ("Under Global Bypass the Transfer Curve Graph dims its curve and dot to 45 % of their alphas, and an edit still moves the curve")
{
    Graphing host;
    host.setCurve (-20.0f, 4.0f, 0.0f, 60.0f);
    REQUIRE (host.drawsWhatPlays());
    host.processor.setDisplayTab (comp1::DisplayTab::transferCurve);
    auto& graph = host.graph();
    REQUIRE (host.settleUntil ([&] { return graph.isShowing(); }));
    CHECK (juce::exactlyEqual (graph.globalBypassFade(), 0.0f));
    CHECK_THAT (graph.curveAlpha(), WithinAbs (1.0, 1.0e-6));
    // The curve at -45 dB in, under Threshold, so -45 dB out: off the grid's 10 dB lines. The graph paints no background
    // of its own, so the curve's alpha is the pixel's.
    const auto at = graph.pointAt (-45.0, -45.0).roundToInt();
    const auto applied = graph.createComponentSnapshot (graph.getLocalBounds(), false);

    host.edit (parameters::globalBypassId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (graph.globalBypassFade(), 1.0f); }));
    CHECK_THAT (graph.curveAlpha(), WithinAbs (0.45, 1.0e-6));
    const auto bypassed = graph.createComponentSnapshot (graph.getLocalBounds(), false);
    CHECK (static_cast<int> (bypassed.getPixelAt (at.x, at.y).getAlpha()) < static_cast<int> (applied.getPixelAt (at.x, at.y).getAlpha()));

    // Dimmed, not frozen: what the compressor would do still follows the settings.
    host.edit (parameters::kneeId, 12.0f);
    CHECK (host.drawsWhatPlays());

    host.edit (parameters::globalBypassId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return juce::exactlyEqual (graph.globalBypassFade(), 0.0f); }));
    CHECK_THAT (graph.curveAlpha(), WithinAbs (1.0, 1.0e-6));
}

TEST_CASE ("An editor opened under Global Bypass shows the Transfer Curve Graph's curve and dot dimmed at once")
{
    Graphing host;
    host.edit (parameters::globalBypassId, 1.0f);
    host.reopen();
    CHECK (juce::exactlyEqual (host.graph().globalBypassFade(), 1.0f));
    CHECK_THAT (host.graph().curveAlpha(), WithinAbs (0.45, 1.0e-6));
}

TEST_CASE ("The Transfer Curve Graph is a read-only image titled Transfer Curve Graph, reading its settings and the Detection Level, with no Tab stop and no mouse input")
{
    Graphing host;
    host.setCurve (-18.0f, 4.0f, 6.0f, 60.0f);
    REQUIRE (host.drawsWhatPlays());
    auto& graph = host.graph();
    auto* handler = graph.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::image);
    CHECK (handler->getTitle() == "Transfer Curve Graph");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK_FALSE (graph.getWantsKeyboardFocus());
    bool clicks = true, childClicks = true;
    graph.getInterceptsMouseClicks (clicks, childClicks);
    CHECK_FALSE (clicks);

    // The settings as their knobs read them (controls::spokenValue); no Detection Level yet: under the scale.
    CHECK (spoken (graph) == "Threshold " + minus + "18.00 dB, Ratio 4:1, Knee 6.0 dB, Detection Level " + minus + "inf dB");
    CHECK (host.keepPlayingUntil (host::steady (-20.0), [&] { return graph.dotDb() && std::abs (*graph.dotDb() + 20.0) < 0.05; }));
    CHECK (spoken (graph) == "Threshold " + minus + "18.00 dB, Ratio 4:1, Knee 6.0 dB, Detection Level " + minus + "20.0 dB");
}
