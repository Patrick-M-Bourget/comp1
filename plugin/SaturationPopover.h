#pragma once

#include "FooterItem.h"
#include "PluginProcessor.h"
#include "SectionPopover.h"
#include "Sections.h"

namespace comp1
{

// The Saturation popover the footer's Saturation readout opens (SectionPopover): the Saturation section
// (SaturationSection), Saturation on/off and Saturation Position over Drive. Opened from the keys, focus moves to the
// on switch.
using SaturationPopover = SectionPopover<SaturationSection>;

// The footer's Saturation readout and the Saturation popover it opens: one footer item (FooterItem), in both views.
using SaturationItem = FooterItem<SaturationPopover>;

// The Saturation readout (plugkit::ui::FooterReadout, titled "Saturation", shrinking to "SAT"): "SAT" and Drive in
// whole dB, signed (saturationReadoutText), drawn in text4, not applied, while Saturation is off or Global Bypass on.
// The footer gives it room for Drive's top.
FooterReading saturationReading (PluginProcessor& processor);

// What the Saturation readout reads: "SAT" and Drive in whole dB, signed, "SAT +6 dB", "SAT 0 dB".
juce::String saturationReadoutText (double driveDb);

} // namespace comp1
