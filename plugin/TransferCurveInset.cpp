#include "TransferCurveInset.h"

#include "Controls.h"
#include "Parameters.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;
namespace knob = staple::tokens::knob;
} // namespace

TransferCurveInset::TransferCurveInset (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      threshold (parameters::parameterOf (p.parameterState(), parameters::thresholdId)),
      ratio (parameters::parameterOf (p.parameterState(), parameters::ratioId)),
      knee (parameters::parameterOf (p.parameterState(), parameters::kneeId)), curve (p, history),
      bypassFade (p.parameterState())
{
    setName ("Transfer Curve inset");
    setTitle ("Transfer Curve inset");
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    // Only the handles take the mouse.
    setInterceptsMouseClicks (false, true);
    curve.setAccessible (false);
    addAndMakeVisible (curve);
    // The Threshold handle last, over the Ratio handle.
    for (const auto which : { InsetHandle::ratio, InsetHandle::threshold })
    {
        auto& h = handleComponents[static_cast<std::size_t> (which)];
        h = std::make_unique<DragHandle> (static_cast<DragHandle::Owner&> (*this), which == InsetHandle::threshold ? "Threshold handle" : "Ratio handle", handleHitRadius,
                                          which == InsetHandle::threshold ? juce::MouseCursor::LeftRightResizeCursor
                                                                          : juce::MouseCursor::UpDownResizeCursor);
        addAndMakeVisible (*h);
    }

    auto& editHistory = processor.session().editHistory();
    controls::showRangeOf (thresholdValue, threshold);
    controls::showRangeOf (ratioValue, ratio);
    controls::showRangeOf (kneeValue, knee);
    thresholdAttachment.emplace (thresholdValue, threshold, editHistory, [this] { processor.stopFollowingComp(); });
    ratioAttachment.emplace (ratioValue, ratio, editHistory, [this] { processor.stopFollowingComp(); });
    kneeAttachment = std::make_unique<juce::SliderParameterAttachment> (knee, kneeValue);
    setSize (side, side);
}

TransferCurveInset::~TransferCurveInset() { endEdit(); }

juce::Component& TransferCurveInset::handle (InsetHandle which) { return *handleComponents[static_cast<std::size_t> (which)]; }

juce::Point<float> TransferCurveInset::handleCentre (InsetHandle which) const
{
    return handleComponents[static_cast<std::size_t> (which)]->centre;
}

InsetHandle TransferCurveInset::whichIs (const DragHandle& handle) const
{
    return &handle == handleComponents[static_cast<std::size_t> (InsetHandle::threshold)].get() ? InsetHandle::threshold : InsetHandle::ratio;
}

handles::Look TransferCurveInset::lookOf (const DragHandle& handle) const { return lookOf (whichIs (handle)); }

handles::Look TransferCurveInset::lookOf (InsetHandle which) const
{
    return { .radius = handleRadius,
             .selected = drag.has_value() && handleOf (drag->setting) == which,
             .hovered = handleComponents[static_cast<std::size_t> (which)]->hovered,
             .alpha = bypassFade.dim (1.0f) };
}

std::optional<TransferCurveInset::Readout> TransferCurveInset::readout() const
{
    if (showsKnee())
        // Unsigned, a width, as the Transfer Curve Graph reads it, to the knob's two decimals.
        return Readout { "Knee", juce::String (kneeValue.getValue(), 2) + " dB", InsetHandle::threshold };
    if (! drag)
        return std::nullopt;
    if (drag->setting == DragSetting::threshold)
        return Readout { "Threshold", controls::spokenValue (threshold, thresholdValue.getValue()), InsetHandle::threshold };
    return Readout { "Ratio", controls::spokenValue (ratio, ratioValue.getValue()), InsetHandle::ratio };
}

bool TransferCurveInset::showsKnee() const
{
    if (drag)
        return drag->setting == DragSetting::knee;
    const auto& h = *handleComponents[static_cast<std::size_t> (InsetHandle::threshold)];
    return h.hovered && h.altHeld;
}

std::optional<juce::Rectangle<float>> TransferCurveInset::kneeShading() const
{
    if (! showsKnee())
        return std::nullopt;
    // From the curve the handles sit on, so the region moves with them.
    const auto played = processor.playedCurve();
    const auto x = [&] (double inputDb) { return curve.pointAt (std::clamp (inputDb, -TransferCurveGraph::scaleDb, 0.0), 0.0).x; };
    const float left = x (played.thresholdDb - played.kneeDb / 2.0), right = x (played.thresholdDb + played.kneeDb / 2.0);
    return juce::Rectangle<float> (left, 0.0f, right - left, static_cast<float> (curve.getHeight())) + curve.getPosition().toFloat();
}

void TransferCurveInset::kneeCueChanged()
{
    auto& h = *handleComponents[static_cast<std::size_t> (InsetHandle::threshold)];
    // The Knee drag is vertical.
    h.setMouseCursor (showsKnee() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    h.updateMouseCursor();
    drawnKneeCue = kneeCue();
    repaint();
}

TransferCurveInset::KneeCue TransferCurveInset::kneeCue() const
{
    if (! showsKnee())
        return {};
    const auto card = readout();
    return { .shading = kneeShading(), .cardAt = handleCentre (card->at), .card = card->value };
}

void TransferCurveInset::resized()
{
    curve.setBounds (getLocalBounds().reduced (padding));
    layOutHandles();
}

void TransferCurveInset::layOutHandles()
{
    const auto played = processor.playedCurve();
    const auto at = [&] (double inputDb) {
        return curve.getPosition().toFloat() + curve.pointAt (inputDb, inputDb - clean::gainReduction (inputDb, played));
    };
    handleComponents[static_cast<std::size_t> (InsetHandle::threshold)]->place (at (played.thresholdDb));
    handleComponents[static_cast<std::size_t> (InsetHandle::ratio)]->place (at (0.0));
}

void TransferCurveInset::paint (juce::Graphics& g)
{
    g.setColour (colour::raised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::r3);
    // Under the graph, which paints no background: the curve draws over it.
    if (const auto shading = kneeShading())
    {
        g.setColour (colour::curveMainHalo.withMultipliedAlpha (bypassFade.dim (1.0f)));
        g.fillRect (*shading);
    }
}

void TransferCurveInset::paintOverChildren (juce::Graphics& g)
{
    if (const auto card = readout())
        handles::paintReadout (g, handleCentre (card->at), handleRadius, static_cast<float> (getWidth()), card->title, card->value);
}

void TransferCurveInset::beginEdit()
{
    if (thresholdEdit.has_value())
        return;
    // Threshold's grab turns Comp Follow off, writing the followed values; Ratio's then finds it off. Both inside the
    // first's transaction, which the second nests in.
    thresholdEdit.emplace (thresholdValue);
    ratioEdit.emplace (ratioValue);
}

void TransferCurveInset::beginKneeEdit()
{
    if (! kneeEdit.has_value())
        kneeEdit.emplace (kneeValue);
}

void TransferCurveInset::endEdit()
{
    kneeEdit.reset();
    ratioEdit.reset();
    thresholdEdit.reset();
}

void TransferCurveInset::setThreshold (double db)
{
    const auto range = parameterRanges::threshold;
    thresholdValue.setValue (std::clamp (db, range.low, range.high), juce::sendNotificationSync);
    layOutHandles();
}

double TransferCurveInset::ratioTravel() const { return ratio.convertTo0to1 (static_cast<float> (ratioValue.getValue())); }

void TransferCurveInset::setRatioTravel (double travel)
{
    ratioValue.setValue (ratio.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))), juce::sendNotificationSync);
    layOutHandles();
}

double TransferCurveInset::kneeTravel() const { return knee.convertTo0to1 (static_cast<float> (kneeValue.getValue())); }

void TransferCurveInset::setKneeTravel (double travel)
{
    kneeValue.setValue (knee.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))), juce::sendNotificationSync);
    layOutHandles();
    repaint();
}

TransferCurveInset::DragSetting TransferCurveInset::settingOf (InsetHandle which, const juce::ModifierKeys& mods)
{
    if (which == InsetHandle::ratio)
        return DragSetting::ratio;
    return mods.isAltDown() ? DragSetting::knee : DragSetting::threshold;
}

InsetHandle TransferCurveInset::handleOf (DragSetting setting)
{
    return setting == DragSetting::ratio ? InsetHandle::ratio : InsetHandle::threshold;
}

void TransferCurveInset::anchorAt (const juce::MouseEvent& e)
{
    // The setting's edit first: Threshold's grab may turn Comp Follow off, and the drag carries on from where that
    // leaves it.
    switch (drag->setting)
    {
        case DragSetting::threshold:
            beginEdit();
            drag->from = thresholdValue.getValue();
            break;
        case DragSetting::ratio:
            beginEdit();
            drag->from = ratioTravel();
            break;
        case DragSetting::knee:
            beginKneeEdit();
            drag->from = kneeTravel();
            break;
    }
    drag->offset = e.getOffsetFromDragStart().toFloat();
}

void TransferCurveInset::grab (DragHandle& handle, const juce::MouseEvent& e)
{
    drag = Drag { .setting = settingOf (whichIs (handle), e.mods) };
    edits.grab (e.mods);
    anchorAt (e);
    // The grab may have turned Comp Follow off, which leaves the curve where it was.
    layOutHandles();
    kneeCueChanged();
}

void TransferCurveInset::dragTo (const juce::MouseEvent& e)
{
    if (! drag)
        return;
    // Shift or Alt pressed or let go mid-drag carries on from where the settings are, at the new speed or setting.
    const auto settingNow = settingOf (handleOf (drag->setting), e.mods);
    if (edits.shiftChanged (e.mods) || settingNow != drag->setting)
    {
        const bool cueChanged = settingNow != drag->setting;
        drag->setting = settingNow;
        anchorAt (e);
        if (cueChanged)
            kneeCueChanged();
    }
    const double speed = edits.dragSpeed();
    const auto moved = (e.getOffsetFromDragStart().toFloat() - drag->offset);
    switch (drag->setting)
    {
        case DragSetting::threshold:
            setThreshold (drag->from + moved.x * TransferCurveGraph::scaleDb / std::max (1, curve.getWidth()) * speed);
            break;
        case DragSetting::ratio:
            setRatioTravel (drag->from + moved.y / knob::dragPixels * speed);
            break;
        case DragSetting::knee:
            setKneeTravel (drag->from - moved.y / knob::dragPixels * speed);
            break;
    }
    repaint();
}

void TransferCurveInset::letGo()
{
    if (! drag)
        return;
    drag.reset();
    edits.letGo();
    kneeCueChanged();
}

bool TransferCurveInset::keyPressed (const juce::KeyPress& key)
{
    const auto arrow = InsetEdit::arrowOf (key);
    if (! arrow)
        return false;
    const bool kneeKey = ! arrow->horizontal && key.getModifiers().isAltDown();
    // Right raises Threshold, up raises Ratio and up widens Knee, as the knobs' arrows do.
    const double by = arrow->direction * arrow->step;
    if (kneeKey)
        beginKneeEdit();
    else
        beginEdit();
    edits.keyHeld();
    if (kneeKey)
        setKneeTravel (kneeTravel() + by);
    else if (arrow->horizontal)
    {
        const double travel = threshold.convertTo0to1 (static_cast<float> (thresholdValue.getValue())) + by;
        setThreshold (threshold.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))));
    }
    else
        setRatioTravel (ratioTravel() + by);
    return true;
}

bool TransferCurveInset::keyStateChanged (bool isKeyDown)
{
    edits.keyStateChanged (isKeyDown);
    return false;
}

void TransferCurveInset::focusLost (FocusChangeType) { edits.focusLost(); }

void TransferCurveInset::tick (double seconds)
{
    // Follows the played curve, whatever moved it, with the handles and the Knee cue.
    layOutHandles();
    if (bypassFade.step (seconds))
        for (auto& h : handleComponents)
            h->repaint();
    if (auto cue = kneeCue(); cue != drawnKneeCue)
    {
        drawnKneeCue = std::move (cue);
        repaint();
    }
    edits.announce (*this, valueText());
}

std::unique_ptr<juce::AccessibilityHandler> TransferCurveInset::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::slider, [this] { return valueText(); });
}

} // namespace comp1
