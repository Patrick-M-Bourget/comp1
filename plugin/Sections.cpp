#include "Sections.h"

#include "Parameters.h"
#include "plugkit/ui/ValueText.h"

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
constexpr auto autoGainTooltip = "Auto Gain: compensates the output level, estimated from the settings, so compression is heard without a loudness bias";
constexpr auto followedAutoGainTooltip = "Auto Gain: Comp Follow holds it on";

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
    limiter.setTitle (parameter (parameters::limiterId).getName (100));
    limiter.setTooltip ("Limiter: keeps the output from passing the Ceiling");
    limiterAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (parameters::limiterId), limiter);
    addSelfPlaced (autoGain);
    addSelfPlaced (limiter);
    follow();
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

bool OutputSection::following() const { return isOn (processor, parameters::compFollowId); }

void OutputSection::follow()
{
    if (following())
    {
        autoGainAttachment.reset();
        autoGain.setToggleState (true, juce::dontSendNotification);
        autoGain.setTooltip (followedAutoGainTooltip);
    }
    else if (autoGainAttachment == nullptr)
    {
        autoGainAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (parameters::autoGainId), autoGain);
        autoGain.setTooltip (autoGainTooltip);
    }
}

bool OutputSection::isControlEnabled (juce::Component& control) const
{
    return &control != &autoGain || ! following();
}

DetectionSection::DetectionSection (PluginProcessor& p) : ControlSection ("Detection", p)
{
    using staple::Icon;
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
