#pragma once

#include "ChoiceChip.h"
#include "Controls.h"
#include "FooterItem.h"
#include "PluginProcessor.h"
#include "SectionPopover.h"

namespace comp1
{

// The Processing Mode popover's section, titled "Processing Mode": the Processing Mode chip (ChoiceChip), reading
// "High Quality" or "Zero Latency"; under it the Lookahead knob; and under that the Oversampling chip, "Oversampling:
// 4x", whose menu offers the factors at or under the cap and ticks the one in force (as the Processing Mode readout
// reads it). Tab walks them in that order. Lookahead and Oversampling are available only in High Quality: in Zero
// Latency they are disabled, not hidden, their values kept; Processing Mode is always available.
class ProcessingModeSection final : public ControlSection
{
public:
    explicit ProcessingModeSection (PluginProcessor& processor);

    int idealWidth() const override;
    int idealHeight() const override;
    void resized() override;

    // The gap between a chip and the knob, and a chip's height.
    static constexpr int chipGap = 12, chipHeight = 24;

private:
    bool isControlEnabled (juce::Component& control) const override;

    PluginProcessor& processor;
    ChoiceChip processingMode, oversampling;
    KnobControl* lookahead = nullptr;
    int chipWidth = 0;
};

// The Processing Mode popover the footer's Processing Mode readout opens (SectionPopover): the Processing Mode section
// (ProcessingModeSection). Opened from the keys, focus moves to Processing Mode.
using ProcessingModePopover = SectionPopover<ProcessingModeSection>;

// The footer's Processing Mode readout and the Processing Mode popover it opens: one footer item (FooterItem), in both
// views.
using ProcessingModeItem = FooterItem<ProcessingModePopover>;

// The Processing Mode readout (plugkit::ui::FooterReadout, titled "Processing Mode"): "High Quality" and the
// Oversampling in force, "High Quality 4x", or "Zero Latency", which has no Oversampling. The Oversampling in force is
// the stored one, or the cap at the sample rate the processor was prepared at (or the assumed one before it is) when
// the stored one is over it (comp1/Latency.h's oversamplingInForce). It is drawn in text4, not applied, while Global
// Bypass is on. The footer gives it room for each of its texts, so it never shrinks.
FooterReading processingModeReading (PluginProcessor& processor);

} // namespace comp1
