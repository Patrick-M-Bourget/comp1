#include "TimingResponseInset.h"

#include "Controls.h"
#include "DisplayFades.h"
#include "GainReductionShading.h"
#include "Parameters.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;
namespace knob = staple::tokens::knob;
namespace display = plugkit::ui::display;

// An arrow's step, in a parameter's travel, as the knobs' (KeyboardSlider): 1%, or 0.2% with Shift.
constexpr double keyStep = 0.01, fineKeyStep = 0.002;
constexpr float curveLineWidth = 1.5f;
// The recovery's dashes, in px: drawn, then skipped.
constexpr float recoveryDashes[] = { 4.0f, 2.0f };
// The labelled times, in ms, and their labels: every other decade.
struct TimeLabel
{
    double ms;
    const char* text;
};
constexpr TimeLabel timeLabels[] = { { 0.01, "10 \xc2\xb5s" }, { 1.0, "1 ms" }, { 100.0, "100 ms" }, { 10000.0, "10 s" } };
constexpr float labelWidth = 34.0f;

double earliestMs() { return TimingResponse::timeMsAt (0); }
double latestMs() { return TimingResponse::timeMsAt (TimingResponse::points - 1); }
} // namespace

TimingResponseInset::TimingResponseInset (PluginProcessor& p)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      attack (parameters::parameterOf (p.parameterState(), parameters::attackId)),
      release (parameters::parameterOf (p.parameterState(), parameters::releaseId)), bypassFade (p.parameterState())
{
    setName ("Timing Response inset");
    setTitle ("Timing Response inset");
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (false);
    // Only the handles take the mouse.
    setInterceptsMouseClicks (false, true);
    // The Attack handle last, over the Release handle.
    for (const auto which : { TimingHandle::release, TimingHandle::attack })
    {
        auto& h = handleComponents[static_cast<std::size_t> (which)];
        h = std::make_unique<DragHandle> (static_cast<DragHandle::Owner&> (*this), which == TimingHandle::attack ? "Attack handle" : "Release handle",
                                          handleHitRadius, juce::MouseCursor::LeftRightResizeCursor);
        addAndMakeVisible (*h);
    }
    controls::showRangeOf (attackValue, attack);
    controls::showRangeOf (releaseValue, release);
    attackAttachment = std::make_unique<juce::SliderParameterAttachment> (attack, attackValue);
    releaseAttachment = std::make_unique<juce::SliderParameterAttachment> (release, releaseValue);
    setSize (side, side);
    follow();
}

TimingResponseInset::~TimingResponseInset() { endEdit(); }

bool TimingResponseInset::isWaiting() const { return asked.has_value() && ! (shown.has_value() && shown->request == *asked); }

juce::Rectangle<float> TimingResponseInset::plot() const
{
    return getLocalBounds().toFloat().reduced (static_cast<float> (padding)).withTrimmedBottom (labelHeight);
}

float TimingResponseInset::xAt (double ms) const
{
    const auto area = plot();
    const double share = std::log10 (std::max (ms, 1.0e-9) / earliestMs()) / std::log10 (latestMs() / earliestMs());
    return area.getX() + static_cast<float> (share) * area.getWidth();
}

double TimingResponseInset::timeAt (float x) const
{
    const auto area = plot();
    const double share = (x - area.getX()) / std::max (1.0f, area.getWidth());
    return earliestMs() * std::pow (latestMs() / earliestMs(), share);
}

float TimingResponseInset::yAt (double gainReductionDb) const
{
    const auto area = plot();
    return area.getY() + static_cast<float> (std::clamp (gainReductionDb / scaleDb, 0.0, 1.0)) * area.getHeight();
}

juce::Component& TimingResponseInset::handle (TimingHandle which) { return *handleComponents[static_cast<std::size_t> (which)]; }

juce::Point<float> TimingResponseInset::handleCentre (TimingHandle which) const
{
    return handleComponents[static_cast<std::size_t> (which)]->centre;
}

TimingHandle TimingResponseInset::whichIs (const DragHandle& h) const
{
    return &h == handleComponents[static_cast<std::size_t> (TimingHandle::attack)].get() ? TimingHandle::attack : TimingHandle::release;
}

handles::Look TimingResponseInset::lookOf (const DragHandle& h) const { return lookOf (whichIs (h)); }

handles::Look TimingResponseInset::lookOf (TimingHandle which) const
{
    return { .radius = handleRadius,
             .selected = drag.has_value() && drag->which == which,
             .hovered = handleComponents[static_cast<std::size_t> (which)]->hovered,
             .alpha = curveAlpha() };
}

std::optional<TimingResponseInset::Readout> TimingResponseInset::readout() const
{
    if (! drag)
        return std::nullopt;
    if (drag->which == TimingHandle::attack)
        return Readout { "Attack", controls::spokenValue (attack, attackValue.getValue()), TimingHandle::attack };
    return Readout { "Release", controls::spokenValue (release, releaseValue.getValue()), TimingHandle::release };
}

juce::String TimingResponseInset::valueText() const
{
    return "Attack " + controls::spokenValue (attack, attackValue.getValue()) + ", Release " + controls::spokenValue (release, releaseValue.getValue());
}

void TimingResponseInset::resized() { layOutHandles(); }

void TimingResponseInset::layOutHandles()
{
    const double attackMs = attackValue.getValue();
    const double releaseFromMs = processor.heldSettings().holdMs + releaseValue.getValue();
    const auto rise = shown ? shown->response.riseAt (attackMs) : 0.0;
    const auto recovery = shown ? shown->response.recoveryAt (releaseFromMs) : 0.0;
    handleComponents[static_cast<std::size_t> (TimingHandle::attack)]->place ({ xAt (attackMs), yAt (rise) });
    handleComponents[static_cast<std::size_t> (TimingHandle::release)]->place ({ xAt (releaseFromMs), yAt (recovery) });
}

void TimingResponseInset::paint (juce::Graphics& g)
{
    g.setColour (colour::raised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::r3);
    const auto area = plot();

    // The grid: a line every decade, the top the zero line. Drawn in the plot's own coordinates.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::translation (area.getX(), area.getY()));
        display::GridLines lines;
        for (double ms = 0.01; ms < latestMs(); ms *= 10.0)
            lines.majorX.push_back (xAt (ms) - area.getX());
        lines.zeroY = 0.0f;
        display::paintGrid (g, lines, { juce::roundToInt (area.getWidth()), juce::roundToInt (area.getHeight()) });
    }

    if (shown)
    {
        const auto& response = shown->response;
        juce::Path rise, recovery;
        const float alpha = curveAlpha();
        float riseEnd = area.getX();
        for (int i = 0; i < TimingResponse::points; ++i)
        {
            const double ms = TimingResponse::timeMsAt (i);
            const auto n = static_cast<std::size_t> (i);
            if (ms <= TimingResponse::burstSeconds * 1000.0)
            {
                const juce::Point<float> at { xAt (ms), yAt (response.rise[n]) };
                i == 0 ? rise.startNewSubPath (at) : rise.lineTo (at);
                riseEnd = at.x;
            }
            const juce::Point<float> at { xAt (ms), yAt (response.recovery[n]) };
            i == 0 ? recovery.startNewSubPath (at) : recovery.lineTo (at);
        }
        // The rise's area, up to the top, shaded as the Level and Gain Reduction Graph shades its Gain Reduction.
        auto riseArea = rise;
        riseArea.lineTo (riseEnd, area.getY());
        riseArea.lineTo (area.getX(), area.getY());
        riseArea.closeSubPath();
        g.setColour (colour::dynRange.withMultipliedAlpha (gainReductionShadingAlpha * alpha));
        g.fillPath (riseArea);
        g.setColour (colour::dynRange.withMultipliedAlpha (alpha));
        g.strokePath (rise, juce::PathStrokeType (curveLineWidth));
        juce::Path dashed;
        juce::PathStrokeType (curveLineWidth).createDashedStroke (dashed, recovery, recoveryDashes, 2);
        g.fillPath (dashed);
    }

    // The time labels, under the plot.
    std::vector<display::Label> scale;
    for (const auto& label : timeLabels)
    {
        const float x = std::clamp (xAt (label.ms), area.getX() + labelWidth / 2.0f, area.getRight() - labelWidth / 2.0f);
        scale.push_back ({ juce::CharPointer_UTF8 (label.text), { x - labelWidth / 2.0f, area.getBottom(), labelWidth, labelHeight },
                           juce::Justification::centred, colour::text3, size::fs1 });
    }
    labels.paint (g, DisplayFades::originInWindow (*this), scale);
}

void TimingResponseInset::paintOverChildren (juce::Graphics& g)
{
    if (const auto card = readout())
        handles::paintReadout (g, handleCentre (card->at), handleRadius, static_cast<float> (getWidth()), card->title, card->value);
}

bool TimingResponseInset::follow()
{
    const TimingResponseWorker::Request now { TimingResponseWorker::shaping (processor.heldSettings()),
                                              plugkit::ui::sampleRateOrAssumed (processor) };
    if (asked != now && worker.ask (now))
        asked = now;
    auto finished = worker.take();
    if (! finished)
        return false;
    shown = std::move (finished);
    double most = 0.0;
    for (const auto* values : { &shown->response.rise, &shown->response.recovery })
        most = std::max (most, *std::max_element (values->begin(), values->end()));
    // At least a dB, so a response with next to no Gain Reduction draws next to the top rather than filling the plot.
    scaleDb = std::max (most, 1.0);
    return true;
}

void TimingResponseInset::beginEdit (TimingHandle which)
{
    auto& edit = which == TimingHandle::attack ? attackEdit : releaseEdit;
    if (! edit.has_value())
        edit.emplace (valueOf (which));
}

void TimingResponseInset::endEdit()
{
    releaseEdit.reset();
    attackEdit.reset();
}

void TimingResponseInset::setTime (TimingHandle which, double ms)
{
    const auto range = which == TimingHandle::attack ? parameterRanges::attack : parameterRanges::release;
    valueOf (which).setValue (std::clamp (ms, range.low, range.high), juce::sendNotificationSync);
    layOutHandles();
}

void TimingResponseInset::anchorAt (const juce::MouseEvent& e)
{
    beginEdit (drag->which);
    drag->fromX = handleCentre (drag->which).x;
    drag->offset = static_cast<float> (e.getDistanceFromDragStartX());
}

void TimingResponseInset::grab (DragHandle& h, const juce::MouseEvent& e)
{
    drag = Drag { .which = whichIs (h), .fine = e.mods.isShiftDown() };
    anchorAt (e);
    repaint();
}

void TimingResponseInset::dragTo (const juce::MouseEvent& e)
{
    if (! drag)
        return;
    // Shift pressed or let go mid-drag carries on from where the handle is, at the new speed.
    if (e.mods.isShiftDown() != drag->fine)
    {
        drag->fine = e.mods.isShiftDown();
        anchorAt (e);
    }
    const double speed = drag->fine ? knob::dragPixels / knob::fineDragPixels : 1.0;
    const double ms = timeAt (drag->fromX + static_cast<float> ((static_cast<float> (e.getDistanceFromDragStartX()) - drag->offset) * speed));
    // The Release handle sits at Hold plus Release.
    setTime (drag->which, drag->which == TimingHandle::attack ? ms : ms - processor.heldSettings().holdMs);
    repaint();
}

void TimingResponseInset::letGo()
{
    if (! drag)
        return;
    drag.reset();
    if (! heldKey)
        endEdit();
    repaint();
}

bool TimingResponseInset::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    if (! horizontal && ! vertical)
        return false;
    const auto which = horizontal ? TimingHandle::attack : TimingHandle::release;
    const double step = key.getModifiers().isShiftDown() ? fineKeyStep : keyStep;
    const int direction = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1 : -1;
    // A press and its repeats are one edit, ended by the key's release or by losing focus.
    beginEdit (which);
    heldKey = true;
    auto& parameter = parameterOf (which);
    const double travel = parameter.convertTo0to1 (static_cast<float> (valueOf (which).getValue())) + direction * step;
    setTime (which, parameter.convertFrom0to1 (static_cast<float> (std::clamp (travel, 0.0, 1.0))));
    repaint();
    return true;
}

bool TimingResponseInset::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        endHeldKey();
    return false;
}

void TimingResponseInset::focusLost (FocusChangeType) { endHeldKey(); }

void TimingResponseInset::endHeldKey()
{
    if (std::exchange (heldKey, false) && ! drag)
        endEdit();
}

void TimingResponseInset::tick (double seconds)
{
    const auto before = std::array { handleCentre (TimingHandle::attack), handleCentre (TimingHandle::release) };
    const bool took = follow();
    layOutHandles();
    const bool faded = bypassFade.step (seconds);
    if (faded)
        for (auto& h : handleComponents)
            h->repaint();
    const bool moved = before != std::array { handleCentre (TimingHandle::attack), handleCentre (TimingHandle::release) };
    if (took || faded || (moved && drag))
        repaint();
    // Tells a screen reader the value changed, when it did.
    const auto now = valueText();
    if (now == announced)
        return;
    announced = now;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

std::unique_ptr<juce::AccessibilityHandler> TimingResponseInset::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::slider, [this] { return valueText(); });
}

} // namespace comp1
