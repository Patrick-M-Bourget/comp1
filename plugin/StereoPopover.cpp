#include "StereoPopover.h"

#include "Parameters.h"

namespace comp1
{

juce::String stereoReadoutText (const juce::String& placement, double linkPercent) { return placement + " " + juce::String (juce::roundToInt (linkPercent)) + "%"; }

FooterReading stereoReading (PluginProcessor& processor)
{
    return { .readout = std::make_unique<plugkit::ui::FooterReadout> ("Stereo", "ST", "Stereo: Stereo Placement and Stereo Link. Click for the stereo controls"),
             .show =
                 [&processor] (plugkit::ui::FooterReadout& readout) {
                     auto& state = processor.parameterState();
                     auto& placement = parameters::parameterOf (state, parameters::stereoPlacementId);
                     const int choice = juce::roundToInt (placement.convertFrom0to1 (placement.getValue()));
                     const double link = state.getRawParameterValue (parameters::stereoLinkId)->load (std::memory_order_relaxed);
                     if (const auto text = stereoReadoutText (placement.getAllValueStrings()[choice], link); text != readout.getButtonText())
                         readout.setButtonText (text);
                     const bool mono = ! (processor.isStereoPlacementAvailable() && processor.isStereoLinkAvailable());
                     if (const bool notApplied = mono || parameters::isOn (*state.getRawParameterValue (parameters::globalBypassId));
                         notApplied != readout.isNotApplied())
                         readout.setNotApplied (notApplied);
                     if (const auto said = mono ? juce::String ("Mono track: Stereo Placement and Stereo Link change nothing") : juce::String();
                         said != readout.getDescription())
                         readout.setDescription (said);
                 },
             .widestTexts = [] (const plugkit::ui::FooterReadout&) -> juce::StringArray { return { stereoReadoutText ("Stereo", 100.0) }; } };
}

} // namespace comp1
