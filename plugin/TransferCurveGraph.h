#pragma once

#include "DisplayFades.h"
#include "GlobalBypassFade.h"
#include "LevelHistory.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "comp1/TransferCurve.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/display/GridLayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace comp1
{

// The Transfer Curve Graph, the Expanded View's Transfer Curve tab: the Transfer Curve the Engine plays, as output
// level over input level, with the Detection Level as a dot moving along it.
//
// Both axes run from scaleDb (-60 dB, the bottom of Threshold's range) to 0 dB: input (the Detection Level) left to
// right, output bottom to top. The curve is output = input less the Transfer Curve's Gain Reduction there
// (clean::gainReduction, the Engine's own function), from the Threshold and Ratio the Engine plays (the Comp Curve's at
// Comp while Comp Follow is on, else the stored ones: PluginProcessor::playedCurve) and the
// stored Knee and Range. So it ignores Host Automation of the stored Threshold and Ratio while Comp Follow is on, as
// the knobs do. The dot sits on the curve at the Detection Level (LevelRecord::detectionLevelDb): each tick the
// loudest read since the last, rising at once and falling at 20 dB/s (LevelBallistics), as the Detection Level ring
// shows it; above 0 dB at the right edge, and not drawn at or under the bottom of the scale. It takes every record of
// the editor's level history for its whole life, shown or not.
//
// It follows the processor on the editor's tick, so a Style, an edit, Host Automation, undo, an A/B switch and a
// restored session all show, and repaints only when the curve's settings change, the dot moves or the Global Bypass
// fade moves.
//
// Under Global Bypass the curve and the dot, which show the compressor's work, dim to a share of their alphas
// (GlobalBypassFade), gliding on the tick, so they read as not applied; the grid and labels stay.
//
// Drawn with stock Staple tokens on plugkit-ui's grid layer: a line every 10 dB on both axes, the 0 dB ones the zero
// lines, with dB labels; the curve a curve-main line and the dot filled in anSc; the edges dissolve into the window's
// background over the design system's display fades. A screen reader finds it as a read-only image titled "Transfer
// Curve Graph", read with the settings it draws and the Detection Level: Threshold and Ratio as their knobs read them
// (controls::spokenValue), Knee unsigned, a width, to one decimal ("Threshold −18.00 dB, Ratio 4:1, Knee 6.0 dB,
// Detection Level −20.0 dB"); it is no stop for Tab and takes no mouse input. Named "Transfer Curve", as its tab,
// which a test finds it by.
//
// The Transfer Curve inset (TransferCurveInset) draws one in its inset look: the same curve, dot and following, the grid
// and labels thinned to every 20 dB to fit, and no fades, as it sits on a slab of its own (provisional).
class TransferCurveGraph final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    // The Transfer Curve tab's, or the Transfer Curve inset's.
    enum class Look
    {
        tab,
        inset
    };

    TransferCurveGraph (PluginProcessor& processor, LevelHistory& history, Look look = Look::tab);

    static constexpr double scaleDb = 60.0, gridStepDb = 10.0, insetGridStepDb = 20.0;

    // The Transfer Curve's settings it draws from.
    const clean::Curve& curve() const { return drawn; }
    // The output level in dB it draws at an input level in dB.
    double outputAt (double inputDb) const { return inputDb - clean::gainReduction (inputDb, drawn); }
    // The Detection Level its dot shows, in dB, -scaleDb to 0; none while the dot isn't drawn.
    std::optional<double> dotDb() const;
    // Where the dot is drawn, in dB: x the Detection Level, y the curve's output there; none while it isn't drawn.
    std::optional<juce::Point<double>> dot() const;
    // Where an input and output level in dB are drawn, from its top left.
    juce::Point<float> pointAt (double inputDb, double outputDb) const;

    // How far into Global Bypass it shows, 0 to 1 (GlobalBypassFade).
    float globalBypassFade() const { return bypassFade.value(); }
    // The curve's and the dot's alpha: 1, dimmed as the fade goes to 1.
    float curveAlpha() const { return bypassFade.dim (1.0f); }

    // The text a screen reader reads as its value: settingsText(), then the Detection Level.
    juce::String valueText() const;
    // The settings it draws, as a screen reader reads them: "Threshold −18.00 dB, Ratio 4:1, Knee 6.0 dB".
    juce::String settingsText() const;

    void paint (juce::Graphics& g) override;

private:
    void tick (double seconds) override;
    bool isActive() const override { return shown > -scaleDb || bypassFade.isMoving(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    PluginProcessor& processor;
    const Look look;
    clean::Curve drawn;
    ShownLevel level; // the Detection Level, on the graph's scale
    double shown = -scaleDb;
    GlobalBypassFade bypassFade;
    plugkit::ui::display::CachedLabels labels;
    DisplayFades fades;
};

} // namespace comp1
