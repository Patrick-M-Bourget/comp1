#pragma once

#include "LevelHistory.h"
#include "LevelRing.h"
#include "PluginProcessor.h"
#include "Sections.h"
#include "plugkit/ui/OutputPopoverFrame.h"

namespace comp1
{

// The output popover the footer's Output readout opens, in both views (plugkit::ui::OutputPopoverFrame): above the
// readout, right-aligned to it, closed by a second click, Escape or a click outside. It holds the Output section
// (OutputSection): Output Gain and Ceiling with its ring side by side, then the A (Auto Gain) and L (Limiter) toggles,
// each attached to its host parameter, so they follow Host Automation, undo, A/B Compare and Presets, and each edit is
// one undo step. Opened from the keys, focus moves to Output Gain, and Tab walks the four in that order; closed, focus
// inside it goes back to the readout.
//
// While open it follows the processor on the editor's tick, Auto Gain showing whether Comp Follow holds it on. Its card
// holds the section, padded; the layout is provisional (#94).
class OutputPopover final : public plugkit::ui::OutputPopoverFrame
{
public:
    OutputPopover (PluginProcessor& processor, LevelHistory& history);

    LevelRing& ceilingRing() { return output.ceilingRing(); }

    void resized() override;

    static constexpr int padding = 12;

private:
    void followProcessor() override { output.followProcessor(); }

    OutputSection output;
};

} // namespace comp1
