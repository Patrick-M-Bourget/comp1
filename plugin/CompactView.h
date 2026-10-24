#pragma once

#include "CompFollowAttachment.h"
#include "Controls.h"
#include "GainReductionMeter.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>

namespace comp1
{

// The Compact View's body, under the editor's shared header and footer, named "Compact View": Gate, Comp and the
// compressor's Gain Reduction meter, left to right in one row, which is Tab's order too. Style is the footer's chip, and
// Output Gain is in the output popover the footer's Output readout opens, as in the Expanded View.
//
// - Gate: its on/off toggle over Gate Threshold, attached as the Expanded View's Gate section attaches them.
// - Comp: the Comp knob alone, attached as the Expanded View's (addCompKnob): grabbing it while Comp Follow is off
//   follows, with the edit that follows as one undo step. There is no Comp Follow toggle, Threshold or Ratio.
// - The Gain Reduction meter: the compressor's, on the editor's level history, as tall as the body, and no stop for Tab.
//
// The row fills the body: Gate at its left edge, the meter at its right, and Comp, at the kit's largest knob size,
// centred in the room between them. The sections keep their own sizes, centred down the body, and the meter its width,
// so a larger window spreads the row out and makes the meter taller. Both views' controls attach to the same host
// parameters, so an edit in one shows in the other.
class CompactView final : public juce::Component
{
public:
    CompactView (PluginProcessor& processor, LevelHistory& history);
    ~CompactView() override;

    GainReductionMeter& gainReductionMeter() { return gainReduction; }

    // The sections' height: the taller's own.
    int sectionsHeight();

    void resized() override;

private:
    // The sections, left to right.
    std::array<Section*, 2> sections() { return { &gate, &compressor }; }

    ControlSection gate, compressor;
    GainReductionMeter gainReduction;
    std::optional<CompFollowAttachment> compAttachment;
};

} // namespace comp1
