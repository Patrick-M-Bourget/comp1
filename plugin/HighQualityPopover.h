#pragma once

#include "ChoiceChip.h"
#include "Controls.h"
#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/FooterPopoverFrame.h"
#include "plugkit/ui/FooterReadout.h"

namespace comp1
{

// The footer's High Quality readout, in both views (plugkit::ui::FooterReadout): "HQ" and the Oversampling in force,
// "HQ 4x", shrinking to "HQ". The Oversampling in force is the stored one, or the cap at the sample rate the processor
// was prepared at (or the assumed one before it is) when the stored one is over it (comp1/Latency.h's
// oversamplingInForce). In Zero Latency, which has no Oversampling, it is drawn not applied, in text4, and still opens
// its popover (HighQualityPopover). It follows the processor on the editor's tick.
class HighQualityReadout final : public plugkit::ui::FooterReadout, private plugkit::ui::EditorTick::Client
{
public:
    explicit HighQualityReadout (PluginProcessor& processor);

    // As wide as its widest text needs, so it keeps its width whichever Oversampling it shows.
    int widestIdealWidth();

private:
    void tick (double) override { show(); }
    void show();

    PluginProcessor& processor;
};

// The High Quality popover's section, titled "High Quality": the Lookahead knob, and under it the Oversampling chip
// (ChoiceChip), "Oversampling: 4x", whose menu offers the factors at or under the cap and ticks the one in force (as
// HighQualityReadout reads it). Tab walks them in that order. Both are available only in High Quality: in Zero Latency
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

// The High Quality popover the footer's High Quality readout opens, in both views (plugkit::ui::FooterPopoverFrame):
// above the readout, right-aligned to it, closed by a second click, Escape or a click outside. It holds the High
// Quality section (HighQualitySection), its controls attached to their host parameters, so they follow Host Automation,
// undo, A/B Compare and Presets, and each edit is one undo step. Opened from the keys, focus moves to Lookahead;
// closed, focus inside it goes back to the readout.
//
// A press in another window, the Oversampling chip's menu, is not a click outside it: it stays open for the pick.
class HighQualityPopover final : public plugkit::ui::FooterPopoverFrame
{
public:
    explicit HighQualityPopover (PluginProcessor& processor);

    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

    static constexpr int padding = 12;

private:
    void followProcessor() override { section.followProcessor(); }

    HighQualitySection section;
};

} // namespace comp1
