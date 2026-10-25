#pragma once

#include "DragHandle.h"
#include "GlobalBypassFade.h"
#include "HandlePainting.h"
#include "PluginProcessor.h"
#include "TimingResponseWorker.h"
#include "TransferCurveInset.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/display/GridLayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <optional>

namespace comp1
{

// The Timing Response inset's two handles.
enum class TimingHandle
{
    attack,  // on the rise, at Attack
    release, // on the recovery, at Hold plus Release
};

// The Timing Response inset: a small graph beside the Transfer Curve inset over the Level tab's top-left corner
// (Display), of the compressor's Gain Reduction over time for a test burst (comp1/TimingResponse.h), and a control that
// moves Attack and Release there.
//
// - **Drawing:** on the Transfer Curve inset's slab, at its size (TransferCurveInset::side square at 100% UI Scale): a
//   log time axis from TimingResponse's earliest time, 5 µs, to about 10 s, left to right, with a line every decade
//   and labels every other; the Gain Reduction hangs from the top, as on the Level and Gain Reduction Graph, scaled so
//   the burst's most reaches the bottom of the plot. The rise, from the burst's start, is a dyn-range line over the
//   burst's second, its area shaded up to the top; the recovery, from the burst's end, a dashed dyn-range line.
// - **The response:** worked out on TimingResponseWorker's thread, never the message thread. Each tick it asks for
//   the response at the settings the host parameters hold (only those that shape it) and the sample rate the Engine
//   plays at, when they changed, and draws the newest finished one: so the curve follows a drag a response or two
//   behind, while the handles follow the pointer at once. Until the first response finishes it draws no curve. Hidden,
//   it asks for nothing, and catches up when it shows again.
// - **Handles:** the Attack handle sits on the rise at Attack, the Release handle on the recovery at Hold plus Release
//   (Release starts once Hold is over), each on the curve drawn, laid out again on the tick and at once after an edit,
//   so they follow Attack, Release and Hold from any route. Drawn as the Transfer Curve inset's are (DragHandle, at its
//   handleRadius), lit while hovered, with a left-right resize cursor, and the selected ring and a readout card
//   ("Attack 10.000 ms", "Release 100.0 ms") while dragged. The Attack handle is drawn over the Release handle.
// - **Global Bypass:** the curves and the handles show the compressor's work, so they dim to a share of their alphas
//   (GlobalBypassFade), gliding on the tick, and still follow and take edits; the slab, grid and labels stay.
// - **Mouse:** only the handles take a press, within TransferCurveInset::handleHitRadius of their centres: a press
//   anywhere else passes through. Each drags its setting horizontally at the time axis's own scale, right longer,
//   Shift finely, by the knobs' proportion. A drag is one undo step, inside gestures the host sees.
// - **Keys:** while focused, left and right step Attack, up and down Release, each 1% of its parameter's travel (0.2%
//   with Shift), as the knobs' arrows do; a key press with its repeats is one undo step.
//
// A screen reader finds it as a slider titled "Timing Response inset", read with Attack and Release as their knobs
// read them ("Attack 10.000 ms, Release 100.0 ms"); it is a Tab stop after the Transfer Curve inset. Its handles are hidden
// from the screen reader and take no focus.
class TimingResponseInset final : public juce::Component, private plugkit::ui::EditorTick::Client, private DragHandle::Owner
{
public:
    explicit TimingResponseInset (PluginProcessor& processor);
    ~TimingResponseInset() override;

    static constexpr int side = TransferCurveInset::side, padding = TransferCurveInset::padding;
    static constexpr float handleRadius = TransferCurveInset::handleRadius, handleHitRadius = TransferCurveInset::handleHitRadius;
    // The time labels' height, under the plot.
    static constexpr float labelHeight = 12.0f;

    // The response it draws: the newest finished, and the request it answers; none before the first.
    const std::optional<TimingResponseWorker::Result>& drawn() const { return shown; }
    // Whether it has asked for a response it hasn't drawn yet.
    bool isWaiting() const;

    // Where the plot is, from the inset's top left, and where a time in ms and a Gain Reduction in dB are drawn in it.
    juce::Rectangle<float> plot() const;
    float xAt (double ms) const;
    float yAt (double gainReductionDb) const;
    // The time in ms at an x, from the inset's left.
    double timeAt (float x) const;

    juce::Component& handle (TimingHandle which);
    juce::Point<float> handleCentre (TimingHandle which) const;
    handles::Look lookOf (TimingHandle which) const;

    // The readout card shown while a handle is dragged: its title, value and handle.
    struct Readout
    {
        juce::String title, value;
        TimingHandle at;
    };
    std::optional<Readout> readout() const;

    // The curves' and the handles' alpha: 1, dimmed under Global Bypass.
    float curveAlpha() const { return bypassFade.dim (1.0f); }
    // The text a screen reader reads as its value.
    juce::String valueText() const;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    struct Drag
    {
        TimingHandle which = TimingHandle::attack;
        bool fine = false;
        float fromX = 0.0f; // the handle's x at the anchor
        float offset = 0.0f; // the drag's offset across from its start at the anchor
    };

    void tick (double seconds) override;
    bool isActive() const override { return bypassFade.isMoving() || (isShowing() && isWaiting()); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    TimingHandle whichIs (const DragHandle& handle) const;
    handles::Look lookOf (const DragHandle& handle) const override;
    void grab (DragHandle& handle, const juce::MouseEvent& e) override;
    void dragTo (const juce::MouseEvent& e) override;
    void letGo() override;
    void anchorAt (const juce::MouseEvent& e);

    juce::Slider& valueOf (TimingHandle which) { return which == TimingHandle::attack ? attackValue : releaseValue; }
    juce::RangedAudioParameter& parameterOf (TimingHandle which) { return which == TimingHandle::attack ? attack : release; }
    void beginEdit (TimingHandle which);
    void endEdit();
    void endHeldKey();
    // Sets which's setting to ms, within its range.
    void setTime (TimingHandle which, double ms);
    // Asks the worker for the response at the settings held, when they changed, and takes any finished one: whether it
    // took one.
    bool follow();
    void layOutHandles();

    PluginProcessor& processor;
    juce::RangedAudioParameter &attack, &release;
    std::array<std::unique_ptr<DragHandle>, 2> handleComponents;
    // What the attachments write from: a slider each, never shown.
    juce::Slider attackValue, releaseValue;
    std::unique_ptr<juce::SliderParameterAttachment> attackAttachment, releaseAttachment;
    std::optional<juce::Slider::ScopedDragNotification> attackEdit, releaseEdit;
    std::optional<Drag> drag;
    bool heldKey = false;
    TimingResponseWorker worker;
    std::optional<TimingResponseWorker::Request> asked; // the last request the worker took
    std::optional<TimingResponseWorker::Result> shown;
    double scaleDb = 1.0; // the Gain Reduction drawn at the plot's bottom: the shown response's most, at least 1 dB
    juce::String announced;
    GlobalBypassFade bypassFade;
    plugkit::ui::display::CachedLabels labels;
};

} // namespace comp1
