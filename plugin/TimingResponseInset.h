#pragma once

#include "DragHandle.h"
#include "GlobalBypassFade.h"
#include "HandlePainting.h"
#include "InsetEdit.h"
#include "PluginProcessor.h"
#include "TimingResponseWorker.h"
#include "TransferCurveInset.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/display/GridLayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace comp1
{

// The Timing Response inset's two handles.
// Each is also the half of the inset it sits in.
enum class TimingHandle
{
    attack,  // on the rise, at Attack, in the left half
    release, // on the recovery, at Hold plus Release, in the right half
};

// The Timing Response inset: a small graph beside the Transfer Curve inset over the Level tab's top-left corner
// (Display), of the compressor's Gain Reduction over time for a test burst (comp1/TimingResponse.h), and a control that
// moves Attack and Release there.
//
// - **Drawing:** on the Transfer Curve inset's slab, width by its height (TransferCurveInset::side) at 100% UI Scale,
//   in two halves side by side, each captioned and with a log time axis of its own, a line every decade and labels
//   under it: Attack's on the left (attackShare of the width), the rise from the burst's start, from Attack's shortest
//   (5 us) to the burst's end (TimingResponse::burstSeconds); Release's on
//   the right, the recovery from the burst's end, from 1 ms to about 10 s. In both the Gain Reduction hangs from the
//   top, as on the Level and Gain Reduction Graph, scaled so the burst's most reaches the bottom of the plot, a
//   dyn-range line with its area shaded up to the top. Each curve runs through the first of the response's times that
//   reads each sample, so between samples it is a straight line in log time, never a step (curveAt).
// - **Bands:** where the response holds a range rather than one curve, a band, filled over the shading between its
//   edges (band). With Auto Release on, Release's half draws the Auto Release band, the recovery at Release to the
//   recovery at 5 x Release, in place of the one recovery, its fastest edge the curve; one sustain level would hide
//   what Auto Release does. On Opto, Attack's half draws the Opto attack band, Attack x 0.5 to Attack x 2 as the
//   overshoot scales it, behind the rise, and Release's half shades the slow part's times, which Release doesn't
//   move, labelled "slow, fixed" (slowPartMs).
// - **The response:** worked out on TimingResponseWorker's thread, never the message thread. Each tick it asks for
//   the response at the settings the host parameters hold (only those that shape it), when they changed, worked out
//   at drawnSampleRate whatever the host plays at, as the response's shape barely changes with the rate and its cost
//   grows with it; and it draws the newest finished one: so the curve follows a drag a response or two
//   behind, while the handles follow the pointer at once. Until the first response finishes it draws no curve. Hidden,
//   it asks for nothing, and catches up when it shows again.
// - **Handles:** the Attack handle sits on the rise at Attack, the Release handle on the recovery at Hold plus Release
//   (Release starts once Hold is over), each on the curve drawn, laid out again on the tick and at once after an edit,
//   so they follow Attack, Release and Hold from any route; every Attack, and every Hold plus Release, lies on
//   its half's axis. Drawn as the Transfer Curve inset's are (DragHandle, at its
//   handleRadius), lit while hovered, with a left-right resize cursor, and the selected ring and a readout card
//   ("Attack 10.000 ms", "Release 100.0 ms") while dragged, beside the handle, never over it, and level with the
//   plots, never over the captions or time labels (handles::readoutBeside).
// - **Global Bypass:** the curves and the handles show the compressor's work, so they dim to a share of their alphas
//   (GlobalBypassFade), gliding on the tick, and still follow and take edits; the slab, grid and labels stay.
// - **Mouse:** only the handles take a press, within TransferCurveInset::handleHitRadius of their centres: a press
//   anywhere else passes through. Each drags its setting horizontally at its half's axis's scale, right longer,
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

    // Its size at 100% UI Scale: as tall as the Transfer Curve inset.
    static constexpr int width = 260, height = TransferCurveInset::side, padding = TransferCurveInset::padding;
    static constexpr float handleRadius = TransferCurveInset::handleRadius, handleHitRadius = TransferCurveInset::handleHitRadius;
    // The captions' height, over the plots, the time labels', under them, and the gap between the halves.
    static constexpr float captionHeight = 14.0f, labelHeight = 12.0f, halfGap = 10.0f;
    // Attack's half's share of the width the two halves have.
    static constexpr float attackShare = 0.42f;
    // Release's half's axis's first time, in ms; its last is TimingResponse's.
    static constexpr double recoveryFromMs = 1.0;

    // The response it draws: the newest finished, and the request it answers; none before the first.
    const std::optional<TimingResponseWorker::Result>& drawn() const { return shown; }
    // Whether it has asked for a response it hasn't drawn yet.
    bool isWaiting() const;

    // Where a half's plot is, from the inset's top left, the times its axis spans, in ms, and where a time in ms (not
    // kept on the axis) and a Gain Reduction in dB are drawn in it.
    juce::Rectangle<float> plot (TimingHandle half) const;
    juce::Range<double> axisMs (TimingHandle half) const;
    float xAt (TimingHandle half, double ms) const;
    float yAt (double gainReductionDb) const;
    // The time in ms at an x, from the inset's left, on a half's axis, past its ends too.
    double timeAt (TimingHandle half, float x) const;
    // The curve a half draws, its vertices as time in ms and Gain Reduction in dB, and its Gain Reduction at any time,
    // straight between vertices in log time and held past its ends; empty, and 0 dB, before the first response.
    const std::vector<juce::Point<double>>& curve (TimingHandle half) const;
    double curveAt (TimingHandle half, double ms) const;
    // A band a half draws, its fastest and slowest edges as vertices as curve() gives them: Opto's attack band on
    // Attack's half, the Auto Release band on Release's, whose fastest edge is that half's curve. None where the
    // response has none.
    struct Band
    {
        std::vector<juce::Point<double>> fastest, slowest;
    };
    const std::optional<Band>& band (TimingHandle half) const { return half == TimingHandle::attack ? riseBand : recoveryBand; }
    // The times Release's half shades and labels fixed on Opto, its slow part's, in ms; none on the other Characters.
    std::optional<juce::Range<double>> slowPartMs() const;

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
    // Where the readout card is drawn, from the inset's top left; none while there is no card.
    std::optional<juce::Rectangle<float>> readoutBounds() const;

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
    // Sets which's setting to ms, within its range.
    void setTime (TimingHandle which, double ms);
    // Asks the worker for the response at the settings held, when they changed, and takes any finished one: whether it
    // took one.
    bool follow();
    void layOutHandles();
    // The points a half draws a curve's vertices through, in the inset's pixels, from its axis's start to its end.
    std::vector<juce::Point<float>> pointsThrough (TimingHandle half, const std::vector<juce::Point<double>>& vertices) const;

    PluginProcessor& processor;
    juce::RangedAudioParameter &attack, &release;
    std::array<std::unique_ptr<DragHandle>, 2> handleComponents;
    // What the attachments write from: a slider each, never shown.
    juce::Slider attackValue, releaseValue;
    std::unique_ptr<juce::SliderParameterAttachment> attackAttachment, releaseAttachment;
    std::optional<juce::Slider::ScopedDragNotification> attackEdit, releaseEdit;
    std::optional<Drag> drag;
    InsetEdit edits { [this] { endEdit(); } };
    TimingResponseWorker worker;
    std::optional<TimingResponseWorker::Request> asked; // the last request the worker took
    std::optional<TimingResponseWorker::Result> shown;
    double scaleDb = 1.0; // the Gain Reduction drawn at the plot's bottom: the shown response's most, at least 1 dB
    // The shown response's curves and bands, as curve() and band() give them.
    std::vector<juce::Point<double>> riseCurve, recoveryCurve;
    std::optional<Band> riseBand, recoveryBand;
    GlobalBypassFade bypassFade;
    plugkit::ui::display::CachedLabels labels;
};

} // namespace comp1
