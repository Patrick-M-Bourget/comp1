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

// A handle: drawn at its centre, and taking a press within handleHitRadius of it, which it hands to the inset.
class TransferCurveInset::Handle final : public juce::Component
{
public:
    Handle (TransferCurveInset& o, InsetHandle w) : owner (o), which (w)
    {
        setName (w == InsetHandle::threshold ? "Threshold handle" : "Ratio handle");
        setAccessible (false);
        setWantsKeyboardFocus (false);
        setMouseCursor (w == InsetHandle::threshold ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
    }

    bool hovered = false;
    juce::Point<float> centre; // in the inset

    // Centred on at, in the inset.
    void place (juce::Point<float> at)
    {
        if (at == centre && ! getBounds().isEmpty())
            return;
        centre = at;
        const float half = handleHitRadius + 1.0f;
        setBounds (juce::Rectangle<float> (2.0f * half, 2.0f * half).withCentre (at).getSmallestIntegerContainer());
        repaint();
    }

    juce::Point<float> localCentre() const { return centre - getPosition().toFloat(); }

    bool hitTest (int x, int y) override
    {
        return juce::Point<float> (static_cast<float> (x) + 0.5f, static_cast<float> (y) + 0.5f).getDistanceFrom (localCentre()) <= handleHitRadius;
    }

    void paint (juce::Graphics& g) override { handles::paint (g, localCentre(), owner.lookOf (which)); }

    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (false); }
    void mouseDown (const juce::MouseEvent& e) override { owner.grab (which, e); }
    void mouseDrag (const juce::MouseEvent& e) override { owner.dragTo (e); }
    void mouseUp (const juce::MouseEvent&) override { owner.letGo(); }

private:
    void setHovered (bool now)
    {
        if (std::exchange (hovered, now) != now)
            repaint();
    }

    TransferCurveInset& owner;
    const InsetHandle which;
};

TransferCurveInset::TransferCurveInset (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      threshold (parameters::parameterOf (p.parameterState(), parameters::thresholdId)),
      ratio (parameters::parameterOf (p.parameterState(), parameters::ratioId)), curve (p, history, TransferCurveGraph::Look::inset)
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
        h = std::make_unique<Handle> (*this, which);
        addAndMakeVisible (*h);
    }

    auto& edits = processor.session().editHistory();
    controls::showRangeOf (thresholdValue, threshold);
    controls::showRangeOf (ratioValue, ratio);
    thresholdAttachment.emplace (thresholdValue, threshold, edits, [this] { processor.stopFollowingComp(); });
    ratioAttachment.emplace (ratioValue, ratio, edits, [this] { processor.stopFollowingComp(); });
    setSize (side, side);
}

TransferCurveInset::~TransferCurveInset() { endEdit(); }

juce::Component& TransferCurveInset::handle (InsetHandle which) { return *handleComponents[static_cast<std::size_t> (which)]; }

juce::Point<float> TransferCurveInset::handleCentre (InsetHandle which) const
{
    return handleComponents[static_cast<std::size_t> (which)]->centre;
}

handles::Look TransferCurveInset::lookOf (InsetHandle which) const
{
    return { .radius = handleRadius,
             .selected = drag.has_value() && drag->handle == which,
             .hovered = handleComponents[static_cast<std::size_t> (which)]->hovered };
}

std::optional<TransferCurveInset::Readout> TransferCurveInset::readout() const
{
    if (! drag)
        return std::nullopt;
    if (drag->handle == InsetHandle::threshold)
        return Readout { "Threshold", controls::spokenValue (threshold, thresholdValue.getValue()) };
    return Readout { "Ratio", controls::spokenValue (ratio, ratioValue.getValue()) };
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
}

void TransferCurveInset::paintOverChildren (juce::Graphics& g)
{
    if (const auto card = readout())
        handles::paintReadout (g, handleCentre (drag->handle), handleRadius, static_cast<float> (getWidth()), card->title, card->value);
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
    layOutHandles();
}

double TransferCurveInset::ratioTravel() const { return ratio.convertTo0to1 (static_cast<float> (ratioValue.getValue())); }

void TransferCurveInset::setRatioTravel (double travel)
{
    ratioValue.setValue (ratio.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))), juce::sendNotificationSync);
    layOutHandles();
}

void TransferCurveInset::anchorAt (const juce::MouseEvent& e)
{
    drag->from = drag->handle == InsetHandle::threshold ? thresholdValue.getValue() : ratioTravel();
    drag->offset = e.getOffsetFromDragStart().toFloat();
}

void TransferCurveInset::grab (InsetHandle which, const juce::MouseEvent& e)
{
    beginEdit();
    drag = Drag { .handle = which, .fine = e.mods.isShiftDown() };
    anchorAt (e);
    // The grab may have turned Comp Follow off, which leaves the curve where it was.
    layOutHandles();
    repaint();
}

void TransferCurveInset::dragTo (const juce::MouseEvent& e)
{
    if (! drag)
        return;
    // Shift pressed or let go mid-drag carries on from where the setting is, at the new speed.
    if (e.mods.isShiftDown() != drag->fine)
    {
        drag->fine = e.mods.isShiftDown();
        anchorAt (e);
    }
    const double speed = drag->fine ? knob::dragPixels / knob::fineDragPixels : 1.0;
    const auto moved = (e.getOffsetFromDragStart().toFloat() - drag->offset);
    if (drag->handle == InsetHandle::threshold)
        setThreshold (drag->from + moved.x * TransferCurveGraph::scaleDb / std::max (1, curve.getWidth()) * speed);
    else
        setRatioTravel (drag->from + moved.y / knob::dragPixels * speed);
    repaint();
}

void TransferCurveInset::letGo()
{
    if (! drag)
        return;
    drag.reset();
    if (! heldKey)
        endEdit();
    repaint();
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
        setRatioTravel (ratioTravel() + direction * step);
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
    // Follows the played curve, whatever moved it.
    layOutHandles();
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
