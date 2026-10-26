#pragma once

#include "FooterItem.h"
#include "LevelHistory.h"
#include "LevelRing.h"
#include "PluginProcessor.h"
#include "SectionPopover.h"
#include "Sections.h"

namespace comp1
{

// The output popover the footer's Output readout opens (SectionPopover): the Output section (OutputSection), Output
// Gain and Ceiling with its ring side by side, then the Dry and Wet sliders, then the A (Auto Gain) and L (Limiter)
// toggles. Opened from the keys, focus moves to Output Gain. The layout is provisional.
using OutputPopover = SectionPopover<OutputSection>;

// The footer's Output readout and the output popover it opens: one footer item (FooterItem), in both views.
using OutputItem = FooterItem<OutputPopover>;

// The Output readout (plugkit::ui::OutputReadout, titled "Output", shrinking to "OUT"): Output Gain plus Auto Gain's
// estimate while Auto Gain is on (PluginProcessor::appliedAutoGainDb), signed to one decimal; Output Gain's range has
// no silent bottom, so it always reads as a level. While Dry is above Off (parallel compression) a "||" mark leads it,
// in its ink, and a screen reader is told so by its description. It is drawn in text4, not applied, while Global
// Bypass is on. While there is an Auto Gain amount to show (shownAutoGain), its description says so, "Includes Auto
// Gain: -4.2 dB", after the parallel mark's. The footer gives it room for what it reads now.
FooterReading outputReading (PluginProcessor& processor);

} // namespace comp1
