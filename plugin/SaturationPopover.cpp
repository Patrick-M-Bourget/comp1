#include "SaturationPopover.h"

#include "Parameters.h"
#include "plugkit/ui/ValueText.h"

namespace comp1
{

juce::String saturationReadoutText (double driveDb) { return "SAT " + plugkit::ui::valueText::signedDb (driveDb, { .decimals = 0 }); }

FooterReading saturationReading (PluginProcessor& processor)
{
    auto& state = processor.parameterState();
    return { .readout = std::make_unique<plugkit::ui::FooterReadout> (
                 "Saturation", "SAT", "Saturation: its Drive, dimmed while Saturation is off or Global Bypass on. Click for Saturation's controls"),
             .show =
                 [&state] (plugkit::ui::FooterReadout& readout) {
                     const double driveDb = state.getRawParameterValue (parameters::driveId)->load (std::memory_order_relaxed);
                     if (const auto text = saturationReadoutText (driveDb); text != readout.getButtonText())
                         readout.setButtonText (text);
                     const bool off = ! parameters::isOn (*state.getRawParameterValue (parameters::saturationId));
                     if (const bool notApplied = off || parameters::isOn (*state.getRawParameterValue (parameters::globalBypassId));
                         notApplied != readout.isNotApplied())
                         readout.setNotApplied (notApplied);
                 },
             .widestTexts = [&state] (const plugkit::ui::FooterReadout&) -> juce::StringArray {
                 return { saturationReadoutText (parameters::parameterOf (state, parameters::driveId).getNormalisableRange().end) };
             } };
}

} // namespace comp1
