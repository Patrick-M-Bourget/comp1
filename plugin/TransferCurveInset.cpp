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

// An arrow's step, in a parameter's travel, as the knobs' (KeyboardSlider): 1%, or 0.2% with Shift.
constexpr double keyStep = 0.01, fineKeyStep = 0.002;
} // namespace

TransferCurveInset::TransferCurveInset (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      threshold (parameters::parameterOf (p.parameterState(), parameters::thresholdId)),
      ratio (parameters::parameterOf (p.parameterState(), parameters::ratioId)), curve (p, history, TransferCurveGraph::Look::inset)
{
    setName ("Transfer Curve inset");
    setTitle ("Transfer Curve inset");
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    curve.setAccessible (false);
    addAndMakeVisible (curve);

    auto& edits = processor.session().editHistory();
    controls::showRangeOf (thresholdValue, threshold);
    controls::showRangeOf (ratioValue, ratio);
    thresholdAttachment.emplace (thresholdValue, threshold, edits, [this] { processor.stopFollowingComp(); });
    ratioAttachment.emplace (ratioValue, ratio, edits, [this] { processor.stopFollowingComp(); });
    setSize (side, side);
}

TransferCurveInset::~TransferCurveInset() { endEdit(); }

void TransferCurveInset::resized() { curve.setBounds (getLocalBounds().reduced (padding)); }

void TransferCurveInset::paint (juce::Graphics& g)
{
    g.setColour (colour::raised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::r3);
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

void TransferCurveInset::endEdit()
{
    ratioEdit.reset();
    thresholdEdit.reset();
}

void TransferCurveInset::setThreshold (double db)
{
    const auto range = parameterRanges::threshold;
    thresholdValue.setValue (std::clamp (db, range.low, range.high), juce::sendNotificationSync);
}

void TransferCurveInset::setRatioTravel (double travel)
{
    ratioValue.setValue (ratio.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))), juce::sendNotificationSync);
}

void TransferCurveInset::anchorAt (const juce::MouseEvent& e)
{
    drag->thresholdDb = thresholdValue.getValue();
    drag->ratioTravel = ratio.convertTo0to1 (static_cast<float> (ratioValue.getValue()));
    drag->offset = e.getOffsetFromDragStart().toFloat();
}

void TransferCurveInset::mouseDown (const juce::MouseEvent& e)
{
    beginEdit();
    drag = Drag { .fine = e.mods.isShiftDown() };
    anchorAt (e);
}

void TransferCurveInset::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag)
        return;
    // Shift pressed or let go mid-drag carries on from where the settings are, at the new speed.
    if (e.mods.isShiftDown() != drag->fine)
    {
        drag->fine = e.mods.isShiftDown();
        anchorAt (e);
    }
    const double speed = drag->fine ? knob::dragPixels / knob::fineDragPixels : 1.0;
    const auto moved = (e.getOffsetFromDragStart().toFloat() - drag->offset);
    const double dbPerPixel = TransferCurveGraph::scaleDb / std::max (1, curve.getWidth());
    setThreshold (drag->thresholdDb + moved.x * dbPerPixel * speed);
    setRatioTravel (drag->ratioTravel + moved.y / knob::dragPixels * speed);
}

void TransferCurveInset::mouseUp (const juce::MouseEvent&)
{
    drag.reset();
    if (! heldKey)
        endEdit();
}

bool TransferCurveInset::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    if (! horizontal && ! vertical)
        return false;
    const double step = key.getModifiers().isShiftDown() ? fineKeyStep : keyStep;
    // Right raises Threshold and up raises Ratio, as the knobs' arrows do.
    const int direction = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1 : -1;
    // A press and its repeats are one edit, ended by the key's release or by losing focus.
    if (! heldKey)
    {
        beginEdit();
        heldKey = true;
    }
    if (horizontal)
    {
        const double travel = threshold.convertTo0to1 (static_cast<float> (thresholdValue.getValue())) + direction * step;
        setThreshold (threshold.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))));
    }
    else
        setRatioTravel (ratio.convertTo0to1 (static_cast<float> (ratioValue.getValue())) + direction * step);
    return true;
}

bool TransferCurveInset::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        endHeldKey();
    return false;
}

void TransferCurveInset::focusLost (FocusChangeType) { endHeldKey(); }

void TransferCurveInset::endHeldKey()
{
    if (std::exchange (heldKey, false) && ! drag)
        endEdit();
}

void TransferCurveInset::tick (double)
{
    // Tells a screen reader the value changed, when it did.
    const auto now = valueText();
    if (now == announced)
        return;
    announced = now;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

std::unique_ptr<juce::AccessibilityHandler> TransferCurveInset::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::slider, [this] { return valueText(); });
}

} // namespace comp1
