#include "TimingResponseInset.h"

#include "Controls.h"
#include "DisplayFades.h"
#include "GainReductionShading.h"
#include "Parameters.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>
#include <span>

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
// A band's fill, over the Gain Reduction's shading, and its edges' lines where they aren't the curve.
constexpr float bandAlpha = 0.22f, bandEdgeWidth = 1.0f, bandEdgeAlpha = 0.6f;
// The label under Opto's slow part's shaded times, and its width.
constexpr const char* slowPartText = "slow, fixed";
constexpr float slowPartLabelWidth = 56.0f;
// The labelled times on each half's axis, in ms, and their labels.
struct TimeLabel
{
    double ms;
    const char* text;
};
constexpr TimeLabel attackLabels[] = { { 0.1, "0.1 ms" }, { 10.0, "10 ms" } };
constexpr TimeLabel releaseLabels[] = { { 10.0, "10 ms" }, { 1000.0, "1 s" } };
constexpr float labelWidth = 34.0f, captionWidth = 60.0f;

// A curve's vertices from a response's values: the first of the points that reads each sample, the times between
// those holding the sample's Gain Reduction, so the curve is straight between samples rather than a step; and the
// last point, so it reaches the axis's end. The response is worked out at drawnSampleRate.
std::vector<juce::Point<double>> verticesOf (const std::array<double, TimingResponse::points>& values)
{
    std::vector<juce::Point<double>> vertices;
    vertices.reserve (values.size());
    long previous = -1;
    for (int i = 0; i < TimingResponse::points; ++i)
    {
        const long sample = TimingResponse::sampleAt (i, drawnSampleRate);
        if (sample != previous || i == TimingResponse::points - 1)
            vertices.push_back ({ TimingResponse::timeMsAt (i), values[static_cast<std::size_t> (i)] });
        previous = sample;
    }
    return vertices;
}

// A curve's Gain Reduction at ms, from its vertices: straight between them in log time, held past its ends.
double along (const std::vector<juce::Point<double>>& vertices, double ms)
{
    if (ms <= vertices.front().x)
        return vertices.front().y;
    if (ms >= vertices.back().x)
        return vertices.back().y;
    const auto after = std::upper_bound (vertices.begin(), vertices.end(), ms, [] (double t, const juce::Point<double>& v) { return t < v.x; });
    const auto& b = *after;
    const auto& a = *(after - 1);
    return a.y + std::log (ms / a.x) / std::log (b.x / a.x) * (b.y - a.y);
}

juce::Path pathThrough (std::span<const juce::Point<float>> points)
{
    juce::Path path;
    path.startNewSubPath (points.front());
    for (const auto& p : points.subspan (1))
        path.lineTo (p);
    return path;
}
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
    setSize (width, height);
    follow();
}

TimingResponseInset::~TimingResponseInset() { endEdit(); }

bool TimingResponseInset::isWaiting() const { return asked.has_value() && ! (shown.has_value() && shown->request == *asked); }

juce::Rectangle<float> TimingResponseInset::plot (TimingHandle half) const
{
    auto area = getLocalBounds().toFloat().reduced (static_cast<float> (padding)).withTrimmedTop (captionHeight).withTrimmedBottom (labelHeight);
    const float attackWidth = (area.getWidth() - halfGap) * attackShare;
    if (half == TimingHandle::attack)
        return area.withWidth (attackWidth);
    return area.withTrimmedLeft (attackWidth + halfGap);
}

juce::Range<double> TimingResponseInset::axisMs (TimingHandle half) const
{
    if (half == TimingHandle::release)
        return { recoveryFromMs, TimingResponse::timeMsAt (TimingResponse::points - 1) };
    // From one sample's time: under it the response has only the first sample's Gain Reduction.
    return { 1000.0 / drawnSampleRate, TimingResponse::burstSeconds * 1000.0 };
}

float TimingResponseInset::xAt (TimingHandle half, double ms) const
{
    const auto area = plot (half);
    const auto axis = axisMs (half);
    const double share = std::log10 (std::max (ms, 1.0e-9) / axis.getStart()) / std::log10 (axis.getEnd() / axis.getStart());
    return area.getX() + static_cast<float> (share) * area.getWidth();
}

double TimingResponseInset::timeAt (TimingHandle half, float x) const
{
    const auto area = plot (half);
    const auto axis = axisMs (half);
    const double share = (x - area.getX()) / std::max (1.0f, area.getWidth());
    return axis.getStart() * std::pow (axis.getEnd() / axis.getStart(), share);
}

float TimingResponseInset::yAt (double gainReductionDb) const
{
    const auto area = plot (TimingHandle::attack);
    return area.getY() + static_cast<float> (std::clamp (gainReductionDb / scaleDb, 0.0, 1.0)) * area.getHeight();
}

const std::vector<juce::Point<double>>& TimingResponseInset::curve (TimingHandle half) const
{
    return half == TimingHandle::attack ? riseCurve : recoveryCurve;
}

double TimingResponseInset::curveAt (TimingHandle half, double ms) const
{
    const auto& vertices = curve (half);
    return vertices.empty() ? 0.0 : along (vertices, ms);
}

std::optional<juce::Range<double>> TimingResponseInset::slowPartMs() const
{
    if (! shown || ! shown->response.slowPartMs)
        return std::nullopt;
    return juce::Range<double> { shown->response.slowPartMs->first, shown->response.slowPartMs->second };
}

std::vector<juce::Point<float>> TimingResponseInset::pointsThrough (TimingHandle half, const std::vector<juce::Point<double>>& vertices) const
{
    const auto area = plot (half);
    const auto axis = axisMs (half);
    std::vector<juce::Point<float>> points;
    points.reserve (vertices.size() + 2);
    points.push_back ({ area.getX(), yAt (along (vertices, axis.getStart())) });
    for (const auto& v : vertices)
        if (v.x > axis.getStart() && v.x <= axis.getEnd())
            points.push_back ({ xAt (half, v.x), yAt (v.y) });
    points.push_back ({ area.getRight(), yAt (along (vertices, axis.getEnd())) });
    return points;
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

std::optional<juce::Rectangle<float>> TimingResponseInset::readoutBounds() const
{
    const auto card = readout();
    if (! card)
        return std::nullopt;
    return handles::readoutBeside (handleCentre (card->at), handleRadius, getLocalBounds().toFloat().reduced (static_cast<float> (padding)),
                                   card->title, card->value);
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
    const auto place = [this] (TimingHandle half, double ms) {
        const auto area = plot (half);
        const float x = std::clamp (xAt (half, ms), area.getX(), area.getRight());
        handleComponents[static_cast<std::size_t> (half)]->place ({ x, yAt (curveAt (half, ms)) });
    };
    place (TimingHandle::attack, attackValue.getValue());
    place (TimingHandle::release, processor.heldSettings().holdMs + releaseValue.getValue());
}

void TimingResponseInset::paint (juce::Graphics& g)
{
    g.setColour (colour::raised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::r3);
    const float alpha = curveAlpha();
    std::vector<display::Label> texts;
    for (const auto half : { TimingHandle::attack, TimingHandle::release })
    {
        const auto area = plot (half);
        const auto axis = axisMs (half);
        // The grid: a line every decade, the top the zero line, in the plot's own coordinates.
        {
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::translation (area.getX(), area.getY()));
            display::GridLines lines;
            for (double ms = std::pow (10.0, std::ceil (std::log10 (axis.getStart()))); ms < axis.getEnd(); ms *= 10.0)
                lines.majorX.push_back (xAt (half, ms) - area.getX());
            lines.zeroY = 0.0f;
            display::paintGrid (g, lines, { juce::roundToInt (area.getWidth()), juce::roundToInt (area.getHeight()) });
        }
        // On Opto, Release's half shades its slow part's times under the curve, labelled at the plot's foot.
        if (const auto span = slowPartMs(); span && half == TimingHandle::release)
        {
            const float from = std::clamp (xAt (half, span->getStart()), area.getX(), area.getRight());
            const float to = std::clamp (xAt (half, span->getEnd()), area.getX(), area.getRight());
            g.setColour (colour::fill1);
            g.fillRect (juce::Rectangle<float> (from, area.getY(), to - from, area.getHeight()));
            const float x = std::clamp ((from + to) / 2.0f, area.getX() + slowPartLabelWidth / 2.0f, area.getRight() - slowPartLabelWidth / 2.0f);
            texts.push_back ({ slowPartText, { x - slowPartLabelWidth / 2.0f, area.getBottom() - labelHeight, slowPartLabelWidth, labelHeight },
                               juce::Justification::centred, colour::text3, size::fs1 });
        }
        // The curve, from the axis's start to its end, with its area up to the top shaded as the Level and Gain
        // Reduction Graph shades its Gain Reduction; and a band, filled between its edges over the shading, its edges
        // drawn thinner where they aren't the curve.
        const auto& vertices = curve (half);
        if (! vertices.empty())
        {
            const auto line = pathThrough (pointsThrough (half, vertices));
            auto shaded = line;
            shaded.lineTo (area.getRight(), area.getY());
            shaded.lineTo (area.getX(), area.getY());
            shaded.closeSubPath();
            g.setColour (colour::dynRange.withMultipliedAlpha (gainReductionShadingAlpha * alpha));
            g.fillPath (shaded);
            if (const auto& edges = band (half))
            {
                const auto fastest = pointsThrough (half, edges->fastest), slowest = pointsThrough (half, edges->slowest);
                auto between = pathThrough (fastest);
                for (auto p = slowest.rbegin(); p != slowest.rend(); ++p)
                    between.lineTo (*p);
                between.closeSubPath();
                g.setColour (colour::dynRange.withMultipliedAlpha (bandAlpha * alpha));
                g.fillPath (between);
                g.setColour (colour::dynRange.withMultipliedAlpha (bandEdgeAlpha * alpha));
                g.strokePath (pathThrough (slowest), juce::PathStrokeType (bandEdgeWidth));
                // Release's band's fastest edge is its curve, drawn next.
                if (half == TimingHandle::attack)
                    g.strokePath (pathThrough (fastest), juce::PathStrokeType (bandEdgeWidth));
            }
            g.setColour (colour::dynRange.withMultipliedAlpha (alpha));
            g.strokePath (line, juce::PathStrokeType (curveLineWidth));
        }
        // The caption over the plot, the time labels under it.
        texts.push_back ({ half == TimingHandle::attack ? "Attack" : "Release",
                           { area.getX(), area.getY() - captionHeight, captionWidth, captionHeight },
                           juce::Justification::centredLeft, colour::text3, size::fs1 });
        for (const auto& label : half == TimingHandle::attack ? std::span<const TimeLabel> (attackLabels) : std::span<const TimeLabel> (releaseLabels))
        {
            const float x = std::clamp (xAt (half, label.ms), area.getX() + labelWidth / 2.0f, area.getRight() - labelWidth / 2.0f);
            texts.push_back ({ juce::CharPointer_UTF8 (label.text), { x - labelWidth / 2.0f, area.getBottom(), labelWidth, labelHeight },
                               juce::Justification::centred, colour::text3, size::fs1 });
        }
    }
    labels.paint (g, DisplayFades::originInWindow (*this), texts);
}

void TimingResponseInset::paintOverChildren (juce::Graphics& g)
{
    if (const auto card = readout())
        handles::paintReadoutAt (g, *readoutBounds(), card->title, card->value);
}

bool TimingResponseInset::follow()
{
    const TimingResponseWorker::Request now { timingResponseShaping (processor.heldSettings()) };
    if (asked != now && worker.ask (now))
        asked = now;
    auto finished = worker.take();
    if (! finished)
        return false;
    shown = std::move (finished);
    const auto& response = shown->response;
    const auto bandOf = [] (const std::optional<TimingResponse::Band>& values) -> std::optional<Band> {
        if (! values)
            return std::nullopt;
        return Band { verticesOf (values->fastest), verticesOf (values->slowest) };
    };
    riseBand = bandOf (response.riseBand);
    recoveryBand = bandOf (response.recoveryBand);
    riseCurve = verticesOf (response.rise);
    // With Auto Release on, the recovery is its band's fastest edge, at Release, where the Release handle sits.
    recoveryCurve = verticesOf (response.recovery);
    double most = 0.0;
    const auto reach = [&most] (const std::array<double, TimingResponse::points>& values) { most = std::max (most, *std::max_element (values.begin(), values.end())); };
    reach (response.rise);
    reach (response.recovery);
    for (const auto* values : { &response.riseBand, &response.recoveryBand })
        if (values->has_value())
        {
            reach ((*values)->fastest);
            reach ((*values)->slowest);
        }
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
    // At the setting's own time, which may lie off the axis, where the handle is held at the end.
    const double ms = drag->which == TimingHandle::attack ? attackValue.getValue() : processor.heldSettings().holdMs + releaseValue.getValue();
    drag->fromX = xAt (drag->which, ms);
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
    const double ms = timeAt (drag->which, drag->fromX + static_cast<float> ((static_cast<float> (e.getDistanceFromDragStartX()) - drag->offset) * speed));
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
    // Hidden (the Spectrum tab, the Compact View), it asks for nothing: it catches up when it shows again.
    const bool took = isShowing() && follow();
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
