#pragma once

#include "ChoiceChip.h"
#include "Controls.h"
#include "FooterItem.h"
#include "PluginProcessor.h"
#include "SectionPopover.h"

namespace comp1
{

// The High Quality popover's section, titled "High Quality": the Lookahead knob, and under it the Oversampling chip
// (ChoiceChip), "Oversampling: 4x", whose menu offers the factors at or under the cap and ticks the one in force (as
// the High Quality readout reads it). Tab walks them in that order. Both are available only in High Quality: in Zero Latency
// they are disabled, not hidden, their values kept.
class HighQualitySection final : public ControlSection
{
public:
    explicit HighQualitySection (PluginProcessor& processor);

    int idealWidth() const override;
    int idealHeight() const override;
    void resized() override;

    // The Oversampling chip's gap under the knob's label, and its height.
    static constexpr int chipTopGap = 12, chipHeight = 24;

private:
    ChoiceChip oversampling;
    int chipWidth = 0;
};

// The High Quality popover the footer's High Quality readout opens (SectionPopover): the High Quality section
// (HighQualitySection). Opened from the keys, focus moves to Lookahead.
using HighQualityPopover = SectionPopover<HighQualitySection>;

// The footer's High Quality readout and the High Quality popover it opens: one footer item (FooterItem), in both views.
using HighQualityItem = FooterItem<HighQualityPopover>;

// The High Quality readout (plugkit::ui::FooterReadout, titled "High Quality", shrinking to "HQ"): "HQ" and the
// Oversampling in force, "HQ 4x". The Oversampling in force is the stored one, or the cap at the sample rate the
// processor was prepared at (or the assumed one before it is) when the stored one is over it (comp1/Latency.h's
// oversamplingInForce). In Zero Latency, which has no Oversampling, it is drawn not applied, in text4, and still opens
// its popover. The footer gives it room for each Oversampling.
FooterReading highQualityReading (PluginProcessor& processor);

} // namespace comp1
