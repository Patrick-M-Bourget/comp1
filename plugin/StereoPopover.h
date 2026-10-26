#pragma once

#include "FooterItem.h"
#include "PluginProcessor.h"
#include "SectionPopover.h"
#include "Sections.h"

namespace comp1
{

// The stereo popover the footer's Stereo readout opens (SectionPopover): the Stereo section (StereoSection), Stereo
// Placement then Stereo Link. Opened from the keys, focus moves to Stereo Placement. On a mono track both are disabled,
// their values kept.
using StereoPopover = SectionPopover<StereoSection>;

// The footer's Stereo readout and the stereo popover it opens: one footer item (FooterItem), in both views.
using StereoItem = FooterItem<StereoPopover>;

// The Stereo readout (plugkit::ui::FooterReadout, titled "Stereo", shrinking to "ST"): Stereo Placement then Stereo
// Link in whole percent (stereoReadoutText), drawn in text4, not applied, while Global Bypass is on, and on a mono
// track, where neither changes anything, which its description tells a screen reader. The footer gives it room for
// "Stereo 100%".
FooterReading stereoReading (PluginProcessor& processor);

// What the Stereo readout reads: Stereo Placement's name, then Stereo Link in whole percent, "Stereo 100%", "Mid 80%".
juce::String stereoReadoutText (const juce::String& placement, double linkPercent);

} // namespace comp1
