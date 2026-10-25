#pragma once

#include "CompFollowAttachment.h"
#include "GlobalBypassFade.h"
#include "HandlePainting.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "TransferCurveGraph.h"
#include "plugkit/ui/EditorTick.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <optional>

namespace comp1
{

// The Transfer Curve inset's two handles.
enum class InsetHandle
{
    threshold, // at the knee: the curve at input = Threshold
    ratio,     // at the curve's right end: input 0 dB
};

// The Transfer Curve inset: a small Transfer Curve Graph over the Level tab's top-left corner (Display), so the
// Transfer Curve and the Detection Level moving along it show beside the levels and Gain Reduction, and a control
// that moves Threshold and Ratio there.
//
// - **Drawing:** a TransferCurveGraph, the curve, the dot and their following, on a raised slab with stock Staple
//   tokens (provisional). side px square at 100% UI Scale.
// - **Handles:** the Threshold handle sits on the curve at the knee (input = Threshold), the Ratio handle at the curve's
//   right end (input 0 dB), both where the Transfer Curve the Engine plays puts them (PluginProcessor::playedCurve),
//   laid out again on the editor's tick and at once after an edit, so they follow Threshold, Ratio and Knee from any
//   route. Each is drawn by the editor's handle look (handles::paint) at handleRadius, lit while the pointer is
//   over it, with a resize cursor of its direction, and the selected ring and a readout card ("Threshold −18.00 dB",
//   "Ratio 4:1") while dragged. The Threshold handle is drawn over the Ratio handle, so at 0 dB Threshold, where they
//   meet, the one that changes the curve is the one grabbed.
// - **Global Bypass:** the curve, the dot and the handles show the compressor's work, so they dim to a share of their
//   alphas (GlobalBypassFade), gliding on the tick, and still follow and take edits; the slab and a readout card stay.
// - **Mouse:** only the handles take a press, within handleHitRadius of their centres: a press anywhere else in the
//   inset passes through it to what lies under it. The Threshold handle drags Threshold horizontally, at the input
//   axis's own scale (scaleDb over the curve's width per pixel); the Ratio handle drags Ratio vertically, by the knobs'
//   travel (staple::tokens::knob::dragPixels for the whole of Ratio's range): up lowers it, straightening the curve's
//   end up, down raises it. Shift drags finely, by the knobs' proportion (fineDragPixels). A double-click is two
//   presses: it resets nothing.
// - **Knee:** Alt on the Threshold handle sets Knee in place of Threshold, vertically by the knobs' travel: up widens
//   it, and Threshold stays put. Shift still drags finely on top. Alt pressed or let go mid-drag carries on from where
//   the settings are, as Shift does. While Alt is held over the handle or the drag is setting Knee, the inset shades
//   the knee region (kneeShading) and the readout card shows "Knee 6.00 dB", over the inset so nothing moves, both
//   following Knee and Threshold from any route on the tick, and the handle's cursor turns up-down. Shift is the knobs' fine drag, so Knee takes Alt, which does nothing else here.
// - **Keys:** while focused, left and right step Threshold, up and down Ratio, and Alt with up and down Knee, each 1% of
//   its parameter's travel (0.2% with Shift), as the knobs' arrows do: right raises Threshold, up raises Ratio and up
//   widens Knee.
// - **Comp Follow:** Threshold and Ratio are written through a CompFollowAttachment each, as the knobs are, so a grab
//   while Comp Follow is on turns it off first (stopFollowingComp) in the same undo step, and the edit carries on from
//   the followed values. Knee isn't followed: setting it, as its knob does, leaves Comp Follow as it is. A drag, or a
//   key press with its repeats, is one undo step, inside gestures the host sees.
//
// A screen reader finds it as a slider titled "Transfer Curve inset", never a parameter's name, read with the settings
// the curve draws (TransferCurveGraph::settingsText); it is a Tab stop after the display's tabs. Its graph and handles
// are hidden from the screen reader and take no focus; the graph takes no mouse input.
class TransferCurveInset final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    TransferCurveInset (PluginProcessor& processor, LevelHistory& history);
    ~TransferCurveInset() override;

    static constexpr int side = 160, padding = 6;
    // A handle's disc and the circle a press grabs it in, in px at 100% UI Scale.
    static constexpr float handleRadius = 6.0f, handleHitRadius = 10.0f;

    // What it draws.
    TransferCurveGraph& graph() { return curve; }
    // The text a screen reader reads as its value.
    juce::String valueText() const { return curve.settingsText(); }

    // A handle, a component of its own that takes the mouse.
    juce::Component& handle (InsetHandle which);
    // Where a handle is drawn, from the inset's top left.
    juce::Point<float> handleCentre (InsetHandle which) const;
    // How a handle draws now: its radius, hovered, selected while dragged, and its alpha, dimmed under Global Bypass.
    handles::Look lookOf (InsetHandle which) const;

    // The readout card shown while a handle is dragged, or Knee's while Alt is held over the Threshold handle: its
    // title and value, and the handle it is shown at, which every card names.
    struct Readout
    {
        Readout (juce::String t, juce::String v, InsetHandle a) : title (std::move (t)), value (std::move (v)), at (a) {}
        juce::String title, value;
        InsetHandle at;
    };
    std::optional<Readout> readout() const;
    // The knee region shaded while Alt is held over the Threshold handle or the drag sets Knee: the inputs from
    // Threshold less half the Knee to Threshold plus half of it, kept on the graph, over its whole height, from the
    // inset's top left; none otherwise.
    std::optional<juce::Rectangle<float>> kneeShading() const;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    class Handle;

    // What a drag sets: Threshold or Ratio by its handle, or Knee by the Threshold handle with Alt.
    enum class DragSetting
    {
        threshold,
        ratio,
        knee,
    };
    static DragSetting settingOf (InsetHandle which, const juce::ModifierKeys& mods);
    static InsetHandle handleOf (DragSetting setting);

    struct Drag
    {
        DragSetting setting = DragSetting::threshold;
        bool fine = false;
        double from = 0.0; // the dragged setting at the anchor: Threshold in dB, or Ratio or Knee in its parameter's travel
        juce::Point<float> offset; // the drag's offset from its start at the anchor
    };

    void tick (double seconds) override;
    bool isActive() const override { return bypassFade.isMoving(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    // Opens the edit: a grab of Threshold and Ratio, one undo step until endEdit(). Overlapping gestures are one step,
    // so the edit may open Knee's too, before or after.
    void beginEdit();
    // Opens Knee's edit alone, leaving Comp Follow as it is.
    void beginKneeEdit();
    void endEdit();
    void endHeldKey();
    void grab (InsetHandle which, const juce::MouseEvent& e);
    void dragTo (const juce::MouseEvent& e);
    void letGo();
    void anchorAt (const juce::MouseEvent& e);
    void setThreshold (double db);
    void setRatioTravel (double travel);
    double ratioTravel() const;
    void setKneeTravel (double travel);
    double kneeTravel() const;
    // Whether the Knee cue shows: Alt held over the Threshold handle, or the drag setting Knee.
    bool showsKnee() const;
    // The Knee cue shown or hidden as showsKnee() says: the shading, the card and the Threshold handle's cursor.
    void kneeCueChanged();
    // What the Knee cue draws: the shading, and the card's text at its handle; none of it while the cue is hidden.
    struct KneeCue
    {
        std::optional<juce::Rectangle<float>> shading;
        juce::Point<float> cardAt;
        juce::String card;
        bool operator== (const KneeCue&) const = default;
    };
    KneeCue kneeCue() const;
    // The handles moved to where the played Transfer Curve puts them.
    void layOutHandles();

    PluginProcessor& processor;
    juce::RangedAudioParameter &threshold, &ratio, &knee;
    TransferCurveGraph curve;
    std::array<std::unique_ptr<Handle>, 2> handleComponents;
    // What the attachments write from: a slider each, never shown.
    juce::Slider thresholdValue, ratioValue, kneeValue;
    std::optional<CompFollowAttachment> thresholdAttachment, ratioAttachment;
    // Knee's, as its knob's: Comp Follow doesn't set it.
    std::unique_ptr<juce::SliderParameterAttachment> kneeAttachment;
    std::optional<juce::Slider::ScopedDragNotification> thresholdEdit, ratioEdit, kneeEdit;
    std::optional<Drag> drag;
    bool heldKey = false;
    // The Knee cue as last drawn: the tick repaints the inset when Knee or Threshold, from any route, changes it.
    KneeCue drawnKneeCue;
    juce::String announced;
    GlobalBypassFade bypassFade;
};

} // namespace comp1
