#include "StereoPopover.h"

#include "ChipWidth.h"
#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace
{
constexpr int edge = staple::tokens::popover::edge;
} // namespace

juce::String stereoReadoutText (const juce::String& placement, double linkPercent) { return placement + " " + juce::String (juce::roundToInt (linkPercent)) + "%"; }

StereoPopover::StereoPopover (PluginProcessor& processor) : stereo (processor)
{
    addAndMakeVisible (stereo);
    const int inset = 2 * (edge + padding);
    setCardSize (stereo.idealWidth() + inset, stereo.idealHeight() + inset);
    followProcessor();
}

void StereoPopover::resized() { stereo.setBounds (getCardBounds().reduced (edge + padding)); }

StereoItem::StereoItem (PluginProcessor& p)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      shown ("Stereo", "ST", "Stereo: Stereo Placement and Stereo Link. Click for the stereo controls"), stereoPopover (p), opens (shown, stereoPopover, *this)
{
    addAndMakeVisible (shown);
    show();
}

int StereoItem::idealWidth() { return widestIdealWidth (shown, { stereoReadoutText ("Stereo", 100.0) }); }

void StereoItem::show()
{
    auto& state = processor.parameterState();
    auto& placement = parameters::parameterOf (state, parameters::stereoPlacementId);
    const int choice = juce::roundToInt (placement.convertFrom0to1 (placement.getValue()));
    const double link = state.getRawParameterValue (parameters::stereoLinkId)->load (std::memory_order_relaxed);
    if (const auto text = stereoReadoutText (placement.getAllValueStrings()[choice], link); text != shown.getButtonText())
        shown.setButtonText (text);
    const bool mono = ! (processor.isStereoPlacementAvailable() && processor.isStereoLinkAvailable());
    if (const bool notApplied = mono || parameters::isOn (*state.getRawParameterValue (parameters::globalBypassId)); notApplied != shown.isNotApplied())
        shown.setNotApplied (notApplied);
    if (const auto said = mono ? juce::String ("Mono track: Stereo Placement and Stereo Link change nothing") : juce::String(); said != shown.getDescription())
        shown.setDescription (said);
}

void StereoItem::resized() { shown.setBounds (getLocalBounds()); }

} // namespace comp1
