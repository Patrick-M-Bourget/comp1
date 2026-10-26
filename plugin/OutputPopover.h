#pragma once

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

} // namespace comp1
