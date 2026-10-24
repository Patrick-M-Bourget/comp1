#include "Sections.h"

#include "Parameters.h"
#include "ShownAutoGain.h"
#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/ValueText.h"
#include "plugkit/ui/staple/Fonts.h"
#include "plugkit/ui/staple/Icons.h"
#include "plugkit/ui/staple/Tokens.h"

#include <cmath>

namespace comp1
{

namespace
{
using Place = ControlSection::Place;

// An on/off host parameter's state, or a two-way choice's second.
bool isOn (PluginProcessor& processor, const juce::String& id)
{
    return processor.parameterState().getRawParameterValue (id)->load (std::memory_order_relaxed) >= 0.5f;
}

} // namespace

ControlSection::KnobControl& addCompKnob (ControlSection& section, PluginProcessor& processor, std::optional<CompFollowAttachment>& attachment, Place place)
{
    auto& comp = section.addKnob (parameters::compId, knobSize::large, "Comp", place, false);
    attachment.emplace (comp.knob, comp.parameter, processor.session().editHistory(), [&processor] { processor.followComp(); });
    return comp;
}

GateSection::GateSection (PluginProcessor& p) : ControlSection ("Gate", p)
{
    addToggle (parameters::gateId, "Gate", true);
    addChoice (parameters::gatePositionId, 64, false);
    addKnob (parameters::gateThresholdId, knobSize::medium, "Threshold", Place::underStack);
    addKnob (parameters::gateRangeId, knobSize::small, "Range");
    addKnob (parameters::gateAttackId, knobSize::small, "Attack");
    addKnob (parameters::gateReleaseId, knobSize::small, "Release");
    addKnob (parameters::gateHoldId, knobSize::small, "Hold");
}

CompressorSection::CompressorSection (PluginProcessor& p, LevelHistory& history) : ControlSection ("Compressor", p), processor (p)
{
    compFollow = &addToggle (parameters::compFollowId, "Comp Follow", true, false);
    comp = &addCompKnob (*this, processor, compAttachment, Place::underStack);
    threshold = &addKnob (parameters::thresholdId, knobSize::large, "Threshold", Place::ownColumn, false);
    ratio = &addKnob (parameters::ratioId, knobSize::medium, "Ratio", Place::ownColumn, false);
    addKnob (parameters::kneeId, knobSize::small, "Knee");
    addKnob (parameters::attackId, knobSize::medium, "Attack");
    addKnob (parameters::releaseId, knobSize::medium, "Release");
    addToggle (parameters::autoReleaseId, "Auto Release", true);
    addKnob (parameters::holdId, knobSize::small, "Hold", Place::underStack);
    addKnob (parameters::rangeId, knobSize::small, "Range");

    ring = std::make_unique<LevelRing> (threshold->knob, threshold->parameter, history, LevelRing::detectionLevel());
    threshold->setRing (*ring);

    auto& edits = processor.session().editHistory();
    thresholdAttachment.emplace (threshold->knob, threshold->parameter, edits, [this] { processor.stopFollowingComp(); }, [this]() -> std::optional<double> {
        return following() ? std::optional (processor.followedThresholdDb()) : std::nullopt;
    });
    ratioAttachment.emplace (ratio->knob, ratio->parameter, edits, [this] { processor.stopFollowingComp(); }, [this]() -> std::optional<double> {
        return following() ? std::optional (processor.followedRatio()) : std::nullopt;
    });

    compFollow->setClickingTogglesState (false);
    compFollow->onClick = [this] {
        if (following())
            processor.stopFollowingComp();
        else
            processor.followComp();
        follow();
    };
    follow();
}

CompressorSection::~CompressorSection()
{
    // Before the knobs the ring is drawn in go.
    threshold->knob.setRing (nullptr);
}

bool CompressorSection::following() const { return isOn (processor, parameters::compFollowId); }

void CompressorSection::follow() { compFollow->setToggleState (following(), juce::dontSendNotification); }

CharacterSection::CharacterSection (PluginProcessor& p) : ControlSection ("Character and Saturation", p)
{
    addKnob (parameters::colourId, knobSize::medium, "Colour");
    addToggle (parameters::saturationId, "Saturation", true);
    addChoice (parameters::saturationPositionId, 84, false);
    addKnob (parameters::driveId, knobSize::medium, "Drive", Place::underStack);
}

namespace
{
// The A toggle's description, its tooltip while Auto Gain is off.
const juce::String autoGainDescription = "Auto Gain: compensates the output level, estimated from the settings, so compression is heard without a loudness bias";

// Sets a Dry or Wet slider up for its parameter: attached, so a double-click sets its default as a knob's does; titled
// and read as a knob is (controls::describe); its readout its name and its level, "Off" at the bottom, else signed to
// 0.1 dB.
std::unique_ptr<juce::SliderParameterAttachment> attachLevel (plugkit::ui::PopoverSlider& slider, juce::RangedAudioParameter& parameter, const juce::String& tooltip)
{
    auto attachment = std::make_unique<juce::SliderParameterAttachment> (parameter, slider);
    slider.setTitle (parameter.getName (100));
    slider.spokenValue = [&parameter] (double plain) { return controls::spokenValue (parameter, plain); };
    slider.valueFromTextFunction = [&parameter] (const juce::String& text) { return controls::valueFromText (parameter, text); };
    slider.readout = [title = slider.getTitle()] (double db) {
        return plugkit::ui::PopoverSlider::Readout { title, {}, std::isinf (db) ? juce::String ("Off") : plugkit::ui::valueText::signedDb (db) };
    };
    slider.setTooltip (tooltip);
    return attachment;
}
} // namespace

OutputSection::OutputSection (PluginProcessor& p, LevelHistory& history)
    : ControlSection ("Output", p), processor (p), dry (plugkit::ui::PopoverSlider::Fill::fromLeft), wet (plugkit::ui::PopoverSlider::Fill::fromLeft),
      autoGain ("Auto Gain", std::nullopt, "A"), limiter ("Limiter", std::nullopt, "L")
{
    addKnob (parameters::outputGainId, knobSize::medium, "Output Gain");
    ceiling = &addKnob (parameters::ceilingId, knobSize::medium, "Ceiling");
    ring = std::make_unique<LevelRing> (ceiling->knob, ceiling->parameter, history, LevelRing::limiterGainReduction());
    ceiling->setRing (*ring);

    dryAttachment = attachLevel (dry, parameter (parameters::dryId), "Dry: the unprocessed signal's level, summed with Wet for parallel compression. Double-click for Off");
    wetAttachment = attachLevel (wet, parameter (parameters::wetId), "Wet: the compressed signal's level. Double-click for 0 dB");
    addSelfPlaced (dry);
    addSelfPlaced (wet);

    autoGain.setTitle (parameter (parameters::autoGainId).getName (100));
    autoGain.setTooltip (autoGainDescription);
    autoGainAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (parameters::autoGainId), autoGain);
    limiter.setTitle (parameter (parameters::limiterId).getName (100));
    limiter.setTooltip ("Limiter: keeps the output from passing the Ceiling");
    limiterAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (parameters::limiterId), limiter);
    addSelfPlaced (autoGain);
    addSelfPlaced (limiter);
}

void OutputSection::follow()
{
    // The amount only while the Auto Gain dot shows it.
    auto tooltip = autoGainDescription;
    if (const auto amount = shownAutoGain (processor))
        tooltip << ". Now " << *amount;
    if (tooltip != autoGain.getTooltip())
        autoGain.setTooltip (tooltip);
}

int OutputSection::idealHeight() const
{
    return ControlSection::idealHeight() + sliderTopGap + 2 * plugkit::ui::PopoverSlider::height + sliderGap + letterRowGap + letterHeight;
}

void OutputSection::resized()
{
    ControlSection::resized();
    auto area = controlArea();
    auto row = area.removeFromBottom (letterHeight);
    area.removeFromBottom (letterRowGap);
    wet.setBounds (area.removeFromBottom (plugkit::ui::PopoverSlider::height));
    area.removeFromBottom (sliderGap);
    dry.setBounds (area.removeFromBottom (plugkit::ui::PopoverSlider::height));
    const int width = (row.getWidth() - letterGap) / 2;
    autoGain.setBounds (row.removeFromLeft (width));
    limiter.setBounds (row.removeFromRight (width));
}

OutputSection::~OutputSection()
{
    // Before the knob the ring is drawn in goes.
    ceiling->knob.setRing (nullptr);
}

// The Detection section's title as a button: the chevron drawn after the title the section paints, text2, text1 while
// hovered or pressed, over the title and the chevron's area.
class DetectionSection::Disclosure final : public juce::Button
{
public:
    explicit Disclosure (DetectionSection& s) : juce::Button ("Detection"), section (s)
    {
        setTitle ("Detection");
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto area = getLocalBounds().toFloat().removeFromRight (static_cast<float> (chevronSize));
        staple::drawIcon (g, section.isOpen() ? staple::Icon::dropdown : staple::Icon::submenu,
                          area.withSizeKeepingCentre (static_cast<float> (chevronSize), static_cast<float> (chevronSize)),
                          highlighted || down ? staple::tokens::colour::text1 : staple::tokens::colour::text2);
    }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return plugkit::ui::accessibility::handler (
            *this, juce::AccessibilityRole::button, [this] { return juce::String (section.isOpen() ? "open" : "collapsed"); }, [this] { juce::NullCheckedInvocation::invoke (onClick); });
    }

    DetectionSection& section;
};

DetectionSection::DetectionSection (PluginProcessor& p)
    : ControlSection ("Detection", p), processor (p), disclosureButton (std::make_unique<Disclosure> (*this))
{
    using staple::Icon;
    // First in Tab's order, before the controls it shows and hides.
    addSelfPlaced (*disclosureButton);
    disclosureButton->onClick = [this] {
        processor.setDetectionOpen (! processor.detectionOpen());
        show (processor.detectionOpen(), true);
    };
    addChoice (parameters::detectionSourceId, 96, true);
    addToggle (parameters::detectionHighPassOnId, "High-Pass", false);
    addKnob (parameters::detectionHighPassFrequencyId, knobSize::small, "Frequency", Place::underStack);
    addKnob (parameters::detectionHighPassSlopeId, knobSize::small, "Slope");
    addToggle (parameters::detectionLowPassOnId, "Low-Pass", true);
    addKnob (parameters::detectionLowPassFrequencyId, knobSize::small, "Frequency", Place::underStack);
    addKnob (parameters::detectionLowPassSlopeId, knobSize::small, "Slope");
    addToggle (parameters::detectionBandOnId, "Free Band", true);
    addEdgeSelector (parameters::detectionBandShapeId, { Icon::bell, Icon::lowShelf, Icon::highShelf }, {}, false);
    addKnob (parameters::detectionBandFrequencyId, knobSize::small, "Frequency", Place::underStack);
    addKnob (parameters::detectionBandGainId, knobSize::small, "Gain");
    addKnob (parameters::detectionBandQId, knobSize::small, "Q");
    audition.setTitle ("Detection Audition");
    audition.setTooltip ("Detection Audition: hold to hear what the detector hears");
    audition.setLitColour (staple::tokens::colour::anSc);
    audition.setClickingTogglesState (false);
    audition.setWantsKeyboardFocus (true);
    audition.onStateChange = [this] { auditionStateChanged(); };
    addSelfPlaced (audition);

    slide.apply = [this] (float) {
        // Hidden once closed, so Tab skips them.
        if (slide.value() <= 0.0f && slide.target() <= 0.0f)
            for (auto* c : body())
                c->setVisible (false);
        disclosureButton->repaint();
        repaint();
        if (onHeightChange != nullptr)
            onHeightChange();
    };
    shownSummary = summaryOfParameters();
    show (processor.detectionOpen(), false);
}

DetectionSection::~DetectionSection()
{
    audition.onStateChange = nullptr;
    if (holdingAudition)
        processor.setDetectionAudition (false);
}

juce::Button& DetectionSection::disclosure() { return *disclosureButton; }

void DetectionSection::auditionStateChanged()
{
    const bool down = audition.getState() == juce::Button::buttonDown;
    if (down == holdingAudition)
        return;
    holdingAudition = down;
    audition.setToggleState (down, juce::dontSendNotification);
    processor.setDetectionAudition (down);
}

void DetectionSection::letGoOfDetectionAudition()
{
    if (holdingAudition)
        audition.setState (juce::Button::buttonNormal);
}

std::vector<juce::Component*> DetectionSection::body()
{
    std::vector<juce::Component*> controls;
    for (auto* c : getChildren())
        if (c != disclosureButton.get())
            controls.push_back (c);
    return controls;
}

void DetectionSection::show (bool open, bool animate)
{
    if (open)
        for (auto* c : body())
            c->setVisible (true);
    else
        letGoOfDetectionAudition();
    if (animate)
        slide.towards (open ? 1.0f : 0.0f);
    else
        slide.jump (open ? 1.0f : 0.0f);
}

void DetectionSection::follow()
{
    if (processor.detectionOpen() != isOpen())
        show (processor.detectionOpen(), false);
    // A restored session lets go of Detection Audition behind the button's back, and a button that isn't showing
    // (the editor's window hidden but not closed) can't be released, so it lets go too.
    if (holdingAudition && (! processor.detectionAudition() || ! audition.isShowing()))
        letGoOfDetectionAudition();
    if (auto now = summaryOfParameters(); now != shownSummary)
    {
        shownSummary = std::move (now);
        repaint();
    }
}

juce::String DetectionSection::summaryOfParameters()
{
    // Frequency to one decimal in kHz: "80 Hz", "2.0 kHz".
    const auto frequency = [this] (const juce::String& id) {
        auto& p = parameter (id);
        return plugkit::ui::valueText::frequency (p.convertFrom0to1 (p.getValue()), { .kHz = 1 });
    };
    const auto band = [&] (const juce::String& onId, const juce::String& name, const juce::String& frequencyId) {
        return name + " " + (isOn (processor, onId) ? frequency (frequencyId) : juce::String ("off"));
    };
    const juce::String dot { juce::CharPointer_UTF8 (" \xc2\xb7 ") };
    return parameter (parameters::detectionSourceId).getCurrentValueAsText() + dot
           + band (parameters::detectionHighPassOnId, "HP", parameters::detectionHighPassFrequencyId) + dot
           + band (parameters::detectionLowPassOnId, "LP", parameters::detectionLowPassFrequencyId) + dot
           + (isOn (processor, parameters::detectionBandOnId)
                  ? band (parameters::detectionBandOnId, parameter (parameters::detectionBandShapeId).getCurrentValueAsText(), parameters::detectionBandFrequencyId)
                  : juce::String ("Free band off"));
}

int DetectionSection::idealWidth() const { return ControlSection::idealWidth() + columnGap + auditionSize; }

int DetectionSection::idealHeight() const
{
    const int open = ControlSection::idealHeight();
    return titleHeight + juce::roundToInt (slide.value() * static_cast<float> (open - titleHeight));
}

void DetectionSection::paint (juce::Graphics& g)
{
    ControlSection::paint (g);
    if (isOpen())
        return;
    g.setFont (staple::font (staple::tokens::size::fs2));
    g.setColour (staple::tokens::colour::text3);
    const int x = disclosureButton->getRight() + summaryGap;
    g.drawText (shownSummary, juce::Rectangle<int> (x, 0, getWidth() - x, titleHeight), juce::Justification::centredLeft, true);
}

void DetectionSection::resized()
{
    // The controls where they sit open, whatever the height: the slide uncovers them.
    ControlSection::resized();
    // Detection Audition in a column of its own after the controls, at their top.
    audition.setBounds (ControlSection::idealWidth() + columnGap, titleHeight + dividerGap, auditionSize, auditionSize);
    disclosureButton->setBounds (0, 0, titleWidth() + chevronGap + chevronSize, titleHeight);
}

StereoSection::StereoSection (PluginProcessor& p) : ControlSection ("Stereo", p)
{
    using staple::Icon;
    // Mid and Side drawn over the whole stereo image, the part they leave unprocessed at 30 %.
    addEdgeSelector (parameters::stereoPlacementId,
                     { Icon::placementStereo, Icon::placementMid, Icon::placementSide },
                     { std::nullopt, Icon::placementStereo, Icon::placementStereo },
                     true);
    addKnob (parameters::stereoLinkId, knobSize::medium, "Stereo Link");
    setAvailability ([&p] { return p.isStereoPlacementAvailable() && p.isStereoLinkAvailable(); });
}

LookaheadSection::LookaheadSection (PluginProcessor& p) : ControlSection ("Lookahead", p)
{
    addKnob (parameters::lookaheadId, knobSize::medium, "Lookahead");
    setAvailability ([&p] { return parameters::inHighQuality (p.parameterState()); });
}

} // namespace comp1
