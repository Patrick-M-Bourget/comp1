#include "DetectionEqDisplay.h"

#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/AssumedSampleRate.h"
#include "plugkit/ui/display/AnalyzerLayer.h"
#include "plugkit/ui/staple/Painting.h"
#include "plugkit/ui/staple/Tokens.h"

#include <plugkit/ShapeDesign.h>

#include <algorithm>
#include <cmath>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace display = plugkit::ui::display;

constexpr float ringWidth = 1.5f, curveWidth = 1.5f, haloWidth = 6.0f;
constexpr int auditionSize = 28, auditionInset = 8;
// The keys' steps: a semitone of Frequency (a tenth of one with Shift), 0.5 dB of Gain (0.05 dB), and a Slope step.
constexpr double semitone = 1.0 / 12.0, gainStepDb = 0.5, fineGainStepDb = 0.05, slopeStep = 6.0;
// A wheel step of Q, in its host parameter's travel.
constexpr float qWheelStep = 0.02f;

float plainOf (const juce::RangedAudioParameter& parameter) { return parameter.convertFrom0to1 (parameter.getValue()); }

juce::String spoken (const juce::RangedAudioParameter& parameter)
{
    return plugkit::ui::accessibility::spokenValue (parameter, parameter.getValue());
}

const DetectionBand& bandOf (const DetectionEq& eq, DetectionEqBand band)
{
    switch (band)
    {
        case DetectionEqBand::highPass: return eq.highPass;
        case DetectionEqBand::freeBand: return eq.freeBand;
        case DetectionEqBand::lowPass: return eq.lowPass;
    }
    return eq.freeBand;
}

} // namespace

// A band's handle: a focus element of its own, which the mouse and the keys move. Its host parameters change inside
// gestures, opened as an edit starts and closed as it ends, so the edit is one undo step the host sees.
class DetectionEqDisplay::Handle final : public juce::Component
{
public:
    Handle (DetectionEqDisplay& d, DetectionEqBand b) : owner (d), band (b)
    {
        auto& state = owner.processor.parameterState();
        const auto parameter = [&state] (const juce::String& id) { return &parameters::parameterOf (state, id); };
        switch (band)
        {
            case DetectionEqBand::highPass:
                on = parameter (parameters::detectionHighPassOnId);
                frequency = parameter (parameters::detectionHighPassFrequencyId);
                slope = parameter (parameters::detectionHighPassSlopeId);
                break;
            case DetectionEqBand::lowPass:
                on = parameter (parameters::detectionLowPassOnId);
                frequency = parameter (parameters::detectionLowPassFrequencyId);
                slope = parameter (parameters::detectionLowPassSlopeId);
                break;
            case DetectionEqBand::freeBand:
                on = parameter (parameters::detectionBandOnId);
                frequency = parameter (parameters::detectionBandFrequencyId);
                gain = parameter (parameters::detectionBandGainId);
                q = parameter (parameters::detectionBandQId);
                shape = parameter (parameters::detectionBandShapeId);
                break;
        }
        setTitle (name() + " handle");
        setName (getTitle());
        setWantsKeyboardFocus (true);
        setRepaintsOnMouseActivity (false);
    }

    ~Handle() override { endEdit(); }

    // "High-pass", "Free band" or "Low-pass": the glossary's names.
    juce::String name() const
    {
        switch (band)
        {
            case DetectionEqBand::highPass: return "High-pass";
            case DetectionEqBand::freeBand: return "Free band";
            case DetectionEqBand::lowPass: return "Low-pass";
        }
        return {};
    }

    bool isOn() const { return on->getValue() >= 0.5f; }
    bool isDragged() const { return drag.has_value(); }

    // The band's settings with their units, as the parameters write them: "120 Hz, 12 dB/oct", or "Bell, 1.00 kHz,
    // +3.00 dB, Q 1.000".
    juce::String settingsText() const
    {
        if (band != DetectionEqBand::freeBand)
            return spoken (*frequency) + ", " + spoken (*slope);
        return shape->getCurrentValueAsText() + ", " + spoken (*frequency) + ", " + spoken (*gain) + ", Q " + spoken (*q);
    }

    // What a screen reader reads: the settings, then "Off" while the band is.
    juce::String valueText() const { return isOn() ? settingsText() : settingsText() + ", Off"; }

    // Tells a screen reader the value changed, when it did.
    void announceChange()
    {
        const auto now = valueText();
        if (now == announced)
            return;
        announced = now;
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    void paint (juce::Graphics& g) override
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float r = display::DisplayGeometry::handleRadius;
        const auto disc = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre);
        const float alpha = isOn() ? 1.0f : staple::tokens::motion::disabledAlpha;
        g.setColour (colour::curveMain.withMultipliedAlpha (alpha));
        g.fillEllipse (disc);
        const bool selected = hasKeyboardFocus (false) || isDragged();
        g.setColour ((selected ? colour::handleSelectedRing : colour::handleRing).withMultipliedAlpha (alpha));
        g.drawEllipse (disc, ringWidth);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        beginEdit();
        drag = Drag { .fine = e.mods.isShiftDown() };
        anchorAt (e);
        owner.refresh();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag)
            return;
        // Shift pressed or let go mid-drag carries on from where the band is, at the new speed.
        if (e.mods.isShiftDown() != drag->fine)
        {
            drag->fine = e.mods.isShiftDown();
            anchorAt (e);
        }
        const float speed = drag->fine ? staple::tokens::knob::dragPixels / staple::tokens::knob::fineDragPixels : 1.0f;
        const auto moved = (e.getOffsetFromDragStart().toFloat() - drag->offset) * speed;
        const auto area = owner.geometry();
        setPlain (*frequency, area.frequencyAt (area.xOf (drag->frequency) + moved.x));
        if (gain != nullptr)
            setPlain (*gain, area.dbAt (area.yOf (drag->gainDb) + moved.y));
        owner.refresh();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        drag.reset();
        endEdit();
        owner.refresh();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
        if (juce::exactlyEqual (delta, 0.0f))
            return;
        const int direction = delta > 0.0f ? 1 : -1;
        const bool alreadyEditing = edit.has_value();
        beginEdit();
        if (q != nullptr)
            q->setValueNotifyingHost (std::clamp (q->getValue() + static_cast<float> (direction) * qWheelStep, 0.0f, 1.0f));
        else
            stepSlope (direction);
        if (! alreadyEditing)
            endEdit();
        owner.refresh();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const int code = key.getKeyCode();
        const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
        const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
        if (! horizontal && ! vertical)
            return false;
        const bool fine = key.getModifiers().isShiftDown();
        const int direction = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1 : -1;
        // A press and its repeats are one edit, ended by the key's release or by losing focus.
        if (! heldKey)
        {
            beginEdit();
            heldKey = true;
        }
        if (horizontal)
            setPlain (*frequency, plainOf (*frequency) * std::pow (2.0, direction * (fine ? semitone / 10.0 : semitone)));
        else if (gain != nullptr)
            setPlain (*gain, plainOf (*gain) + direction * (fine ? fineGainStepDb : gainStepDb));
        else
            stepSlope (direction);
        owner.refresh();
        return true;
    }

    bool keyStateChanged (bool isKeyDown) override
    {
        if (! isKeyDown)
            endHeldKey();
        return false;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override
    {
        endHeldKey();
        repaint();
    }

private:
    struct Drag
    {
        bool fine = false;
        double frequency = 0.0, gainDb = 0.0; // where the band was at the anchor
        juce::Point<float> offset;            // the drag's offset from its start at the anchor
    };

    void anchorAt (const juce::MouseEvent& e)
    {
        drag->frequency = plainOf (*frequency);
        drag->gainDb = gain != nullptr ? plainOf (*gain) : 0.0;
        drag->offset = e.getOffsetFromDragStart().toFloat();
    }

    // Opens the edit: a gesture on each of the band's host parameters, so whatever changes is one undo step, and the
    // band turned on in it.
    void beginEdit()
    {
        if (edit)
            return;
        edit.emplace();
        for (auto* p : { on, frequency, slope, gain, q })
            if (p != nullptr)
            {
                p->beginChangeGesture();
                edit->push_back (p);
            }
        if (! isOn())
            on->setValueNotifyingHost (1.0f);
    }

    void endEdit()
    {
        if (! edit)
            return;
        for (auto* p : *edit)
            p->endChangeGesture();
        edit.reset();
    }

    void endHeldKey()
    {
        if (std::exchange (heldKey, false) && ! drag)
            endEdit();
    }

    static void setPlain (juce::RangedAudioParameter& parameter, double value)
    {
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (value)));
    }

    void stepSlope (int direction)
    {
        const auto range = parameterRanges::detectionSlope;
        setPlain (*slope, std::clamp (plainOf (*slope) + direction * slopeStep, range.low, range.high));
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::slider, [this] { return valueText(); });
    }

    DetectionEqDisplay& owner;
    const DetectionEqBand band;
    juce::RangedAudioParameter *on = nullptr, *frequency = nullptr, *slope = nullptr, *gain = nullptr, *q = nullptr, *shape = nullptr;
    std::optional<std::vector<juce::RangedAudioParameter*>> edit; // the gestures open
    std::optional<Drag> drag;
    bool heldKey = false;
    juce::String announced;
};

DetectionEqDisplay::DetectionEqDisplay (PluginProcessor& p)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p), reader (p.parameterState()),
      sampleRate (plugkit::ui::sampleRateOrAssumed (p))
{
    setName ("Detection EQ display");
    setTitle ("Detection EQ");
    analyzer.showPreEq = false;
    analyzer.showPostEq = false;
    analyzer.showSidechain = true;
    analyzer.peakHold = false;
    detectionSpectrum.prepare (sampleRate, analyzer.resolution);
    int order = 0;
    for (const auto band : { DetectionEqBand::highPass, DetectionEqBand::freeBand, DetectionEqBand::lowPass })
    {
        auto& h = handles[static_cast<std::size_t> (band)];
        h = std::make_unique<Handle> (*this, band);
        h->setExplicitFocusOrder (++order);
        addAndMakeVisible (*h);
    }
    audition.setTitle ("Detection Audition");
    audition.setTooltip ("Detection Audition: hold to hear what the detector hears");
    audition.setLitColour (colour::anSc);
    audition.setClickingTogglesState (false);
    audition.setWantsKeyboardFocus (true);
    audition.setExplicitFocusOrder (++order);
    audition.onStateChange = [this] { auditionStateChanged(); };
    addAndMakeVisible (audition);
    // What piled up while no editor read the tap.
    processor.skipDetectionSourceSamples();
    if (readSettings())
        settingsChanged();
}

DetectionEqDisplay::~DetectionEqDisplay()
{
    audition.onStateChange = nullptr;
    if (holdingAudition)
        processor.setDetectionAudition (false);
}

juce::Component& DetectionEqDisplay::handle (DetectionEqBand band) { return *handles[static_cast<std::size_t> (band)]; }

display::DisplayGeometry DetectionEqDisplay::geometry() const
{
    return { .width = getWidth(), .height = getHeight(), .rangeDb = displayRangeDb };
}

double DetectionEqDisplay::responseDbAt (double frequency) const
{
    double magnitude = 1.0;
    for (const auto& cascade : onCascades)
        magnitude *= std::abs (plugkit::responseAt (cascade, frequency, sampleRate));
    return 20.0 * std::log10 (std::max (magnitude, 1.0e-12));
}

double DetectionEqDisplay::curveDbAt (double frequency) const { return responseDbAt (frequency); }

juce::Point<float> DetectionEqDisplay::handleCentre (DetectionEqBand band) const
{
    const auto shape = geometry();
    const auto& settings = bandOf (eq, band);
    const double frequency = settings.filter.frequency;
    const bool atGain = band == DetectionEqBand::freeBand && settings.filter.shape == plugkit::Shape::Bell;
    const double db = atGain ? settings.filter.gain : responseDbAt (frequency);
    const auto range = static_cast<double> (displayRangeDb);
    return { shape.xOf (frequency), shape.yOf (std::clamp (db, -range, range)) };
}

bool DetectionEqDisplay::readSettings()
{
    const auto now = reader.read().detectionEq;
    if (now == eq)
        return false;
    eq = now;
    return true;
}

void DetectionEqDisplay::refresh()
{
    if (readSettings())
        settingsChanged();
    for (auto& h : handles)
        h->repaint();
    repaint();
}

void DetectionEqDisplay::settingsChanged()
{
    onCascades.clear();
    for (const auto band : { DetectionEqBand::highPass, DetectionEqBand::freeBand, DetectionEqBand::lowPass })
        if (const auto& settings = bandOf (eq, band); settings.on)
            onCascades.push_back (plugkit::designBand (settings.filter, sampleRate));
    const auto shape = geometry();
    curve.clear();
    const auto range = static_cast<double> (displayRangeDb) * 2.0;
    for (float x = 0.0f; x <= static_cast<float> (shape.width); x += display::DisplayGeometry::pixelStep)
    {
        const juce::Point<float> point { x, shape.yOf (std::clamp (responseDbAt (shape.frequencyAt (x)), -range, range)) };
        if (juce::exactlyEqual (x, 0.0f))
            curve.startNewSubPath (point);
        else
            curve.lineTo (point);
    }
    layOutHandles();
    for (auto& h : handles)
        h->announceChange();
    repaint();
}

void DetectionEqDisplay::layOutHandles()
{
    const float side = 2.0f * (display::DisplayGeometry::handleRadius + ringWidth) + 2.0f;
    for (const auto band : { DetectionEqBand::highPass, DetectionEqBand::freeBand, DetectionEqBand::lowPass })
        handle (band).setBounds (juce::Rectangle<float> (side, side).withCentre (handleCentre (band)).getSmallestIntegerContainer());
}

void DetectionEqDisplay::readTap()
{
    tapBuffer.resize (static_cast<std::size_t> (std::max (1, processor.detectionSourceSampleCapacity())));
    const int read = processor.readDetectionSourceSamples (tapBuffer.data(), static_cast<int> (tapBuffer.size()));
    samplesArriving = read > 0;
    if (read > 0)
        detectionSpectrum.push (tapBuffer.data(), read);
}

void DetectionEqDisplay::tick (double seconds)
{
    if (const double rate = plugkit::ui::sampleRateOrAssumed (processor); ! juce::exactlyEqual (rate, sampleRate))
    {
        sampleRate = rate;
        detectionSpectrum.prepare (sampleRate, analyzer.resolution);
        settingsChanged();
    }
    // Drained whichever tab shows, so the tap never fills.
    readTap();
    if (readSettings())
        settingsChanged();
    for (auto& h : handles)
        h->announceChange();
    // A restored session lets go of Detection Audition behind the button's back.
    if (holdingAudition && ! processor.detectionAudition())
        letGoOfDetectionAudition();
    // Analysed and repainted only while shown, and only as samples arrive: with no audio it holds still.
    if (isShowing())
    {
        if (samplesArriving)
        {
            detectionSpectrum.update (seconds, analyzer.speed);
            repaint();
        }
    }
    else if (holdingAudition)
        letGoOfDetectionAudition();
}

bool DetectionEqDisplay::isActive() const { return isShowing() && samplesArriving; }

void DetectionEqDisplay::auditionStateChanged()
{
    const bool down = audition.getState() == juce::Button::buttonDown;
    if (down == holdingAudition)
        return;
    holdingAudition = down;
    audition.setToggleState (down, juce::dontSendNotification);
    processor.setDetectionAudition (down);
}

void DetectionEqDisplay::letGoOfDetectionAudition()
{
    if (holdingAudition)
        audition.setState (juce::Button::buttonNormal);
}

void DetectionEqDisplay::resized()
{
    audition.setBounds (getWidth() - auditionInset - auditionSize, auditionInset, auditionSize, auditionSize);
    settingsChanged();
}

void DetectionEqDisplay::paint (juce::Graphics& g)
{
    const auto shape = geometry();
    display::paintGrid (g, shape);
    display::paintAnalyzer (g, shape, { .settings = analyzer, .preEq = detectionSpectrum, .postEq = detectionSpectrum, .sidechain = detectionSpectrum });
    g.setColour (colour::curveMainHalo);
    g.strokePath (curve, juce::PathStrokeType (haloWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour::curveMain);
    g.strokePath (curve, juce::PathStrokeType (curveWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    fades.paint (g, *this);
    // The labels go over the fades, unfaded, from images aligned with the window's device pixels.
    gridLabelImages.paint (g, DisplayFades::originInWindow (*this), display::gridLabels (shape));
}

void DetectionEqDisplay::paintOverChildren (juce::Graphics& g)
{
    // The dragged band's readout card, above its handle and inside the display.
    for (auto& h : handles)
        if (h->isDragged())
        {
            const auto value = h->settingsText();
            const float width = staple::readoutCardWidth (h->name(), value);
            const auto height = static_cast<float> (staple::readoutCard::height);
            const auto centre = h->getBounds().toFloat().getCentre();
            auto card = juce::Rectangle<float> (width, height).withCentre (centre.translated (0.0f, -(display::DisplayGeometry::handleRadius + 8.0f + height / 2.0f)));
            if (card.getY() < 0.0f)
                card.setY (centre.y + display::DisplayGeometry::handleRadius + 8.0f);
            card.setX (std::clamp (card.getX(), 0.0f, std::max (0.0f, static_cast<float> (getWidth()) - width)));
            staple::drawReadoutCard (g, card, h->name(), value);
        }
}

std::unique_ptr<juce::AccessibilityHandler> DetectionEqDisplay::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::group, [] { return juce::String(); });
}

} // namespace comp1
