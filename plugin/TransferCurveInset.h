#pragma once

#include "CompFollowAttachment.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "TransferCurveGraph.h"
#include "plugkit/ui/EditorTick.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace comp1
{

// The Transfer Curve inset: a small Transfer Curve Graph over the Level tab's top-left corner (Display), so the
// Transfer Curve and the Detection Level moving along it show beside the levels and Gain Reduction, and a control
// that moves Threshold and Ratio there.
//
// - **Drawing:** a TransferCurveGraph in its inset look, so the same curve, dot and following as the Transfer Curve
//   tab's, on a raised slab with stock Staple tokens (provisional, #94). side px square at 100% UI Scale.
// - **Mouse:** a drag moves Threshold horizontally, at the input axis's own scale (scaleDb over the curve's width per
//   pixel), and Ratio vertically, by the knobs' travel (staple::tokens::knob::dragPixels for the whole of Ratio's
//   range): down raises it, bending the curve over Threshold down, up lowers it. Shift drags finely, by the knobs'
//   proportion (fineDragPixels). A double-click is two presses: it resets nothing.
// - **Keys:** while focused, left and right step Threshold, up and down Ratio, each 1% of its parameter's travel
//   (0.2% with Shift), as the knobs' arrows do: right raises Threshold and up raises Ratio.
// - **Comp Follow:** Threshold and Ratio are written through a CompFollowAttachment each, as the knobs are, so a grab
//   while Comp Follow is on turns it off first (stopFollowingComp) in the same undo step, and the edit carries on from
//   the followed values. A drag, or a key press with its repeats, is one undo step, inside gestures the host sees.
//
// A screen reader finds it as a slider titled "Transfer Curve inset", never a parameter's name, read with the settings
// the curve draws (TransferCurveGraph::settingsText); it is a Tab stop after the display's tabs. Its graph is hidden from
// the screen reader, takes no mouse input and no focus.
class TransferCurveInset final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    TransferCurveInset (PluginProcessor& processor, LevelHistory& history);
    ~TransferCurveInset() override;

    static constexpr int side = 160, padding = 6;

    // What it draws.
    TransferCurveGraph& graph() { return curve; }
    // The text a screen reader reads as its value.
    juce::String valueText() const { return curve.settingsText(); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    struct Drag
    {
        bool fine = false;
        double thresholdDb = 0.0, ratioTravel = 0.0; // where they were at the anchor; Ratio in its parameter's travel
        juce::Point<float> offset;                   // the drag's offset from its start at the anchor
    };

    void tick (double) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    // Opens the edit: a grab of both, one undo step until endEdit().
    void beginEdit();
    void endEdit();
    void endHeldKey();
    void anchorAt (const juce::MouseEvent& e);
    void setThreshold (double db);
    void setRatioTravel (double travel);

    PluginProcessor& processor;
    juce::RangedAudioParameter &threshold, &ratio;
    TransferCurveGraph curve;
    // What the attachments write from: a slider each, never shown.
    juce::Slider thresholdValue, ratioValue;
    std::optional<CompFollowAttachment> thresholdAttachment, ratioAttachment;
    std::optional<juce::Slider::ScopedDragNotification> thresholdEdit, ratioEdit;
    std::optional<Drag> drag;
    bool heldKey = false;
    juce::String announced;
};

} // namespace comp1
