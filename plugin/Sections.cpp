#include "Sections.h"

#include "Parameters.h"

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

bool inHighQuality (PluginProcessor& processor) { return isOn (processor, parameters::processingModeId); }
} // namespace

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
    autoGain = &addToggle (parameters::autoGainId, "Auto Gain", false, false);
    comp = &addKnob (parameters::compId, knobSize::large, "Comp", Place::underStack, false);
    threshold = &addKnob (parameters::thresholdId, knobSize::large, "Threshold", Place::ownColumn, false);
    ratio = &addKnob (parameters::ratioId, knobSize::medium, "Ratio", Place::ownColumn, false);
    addKnob (parameters::kneeId, knobSize::small, "Knee");
    addKnob (parameters::attackId, knobSize::medium, "Attack");
    addKnob (parameters::releaseId, knobSize::medium, "Release");
    addToggle (parameters::autoReleaseId, "Auto Release", true);
    addKnob (parameters::holdId, knobSize::small, "Hold", Place::underStack);
    addKnob (parameters::rangeId, knobSize::small, "Range");

    ring = std::make_unique<DetectionLevelRing> (threshold->knob, threshold->parameter, history);
    threshold->setRing (*ring);

    auto& edits = processor.session().editHistory();
    compAttachment.emplace (comp->knob, comp->parameter, edits, [this] { processor.followComp(); });
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

void CompressorSection::follow()
{
    const bool on = following();
    compFollow->setToggleState (on, juce::dontSendNotification);
    if (on && autoGainAttachment != nullptr)
    {
        autoGainAttachment.reset();
        autoGain->setTooltip ("Auto Gain: Comp Follow holds it on");
    }
    if (on)
        autoGain->setToggleState (true, juce::dontSendNotification);
    else if (autoGainAttachment == nullptr)
    {
        autoGainAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (parameters::autoGainId), *autoGain);
        autoGain->setTooltip (autoGain->getTitle());
    }
}

bool CompressorSection::isControlEnabled (juce::Component& control) const
{
    return &control != autoGain || ! following();
}

CharacterSection::CharacterSection (PluginProcessor& p) : ControlSection ("Character and Saturation", p)
{
    addKnob (parameters::colourId, knobSize::medium, "Colour");
    addToggle (parameters::saturationId, "Saturation", true);
    addChoice (parameters::saturationPositionId, 84, false);
    addKnob (parameters::driveId, knobSize::medium, "Drive", Place::underStack);
}

DryWetSection::DryWetSection (PluginProcessor& p) : ControlSection ("Dry/Wet", p)
{
    addKnob (parameters::dryId, knobSize::medium, "Dry");
    addKnob (parameters::wetId, knobSize::medium, "Wet");
}

OutputSection::OutputSection (PluginProcessor& p) : ControlSection ("Output", p)
{
    addKnob (parameters::outputGainId, knobSize::medium, "Output Gain");
    addToggle (parameters::limiterId, "Limiter", true);
    addKnob (parameters::ceilingId, knobSize::medium, "Ceiling", Place::underStack);
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
    setAvailability ([&p] { return inHighQuality (p); });
}

} // namespace comp1
