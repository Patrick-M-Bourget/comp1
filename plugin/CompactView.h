#pragma once

#include "CompFollowAttachment.h"
#include "Controls.h"
#include "GainReductionMeter.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "StyleSection.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>

namespace comp1
{

// The Compact View's body, under the editor's shared header and footer, named "Compact View": Style, Gate, Comp and
// the compressor's Gain Reduction meter, left to right in one row, which is Tab's order too. Output Gain is the
// footer's, in the output popover its Output readout opens, as in the Expanded View.
//
// - Style: a StyleSection, which chooses through PluginProcessor::chooseStyle, as in the Expanded View.
// - Gate: its on/off toggle over Gate Threshold, attached as the Expanded View's Gate section attaches them.
// - Comp: the Comp knob alone, attached as the Expanded View's (addCompKnob): grabbing it while Comp Follow is off
//   follows, with the edit that follows as one undo step. There is no Comp Follow toggle, Threshold or Ratio.
// - The Gain Reduction meter: the compressor's, on the editor's level history, as tall as the body, and no stop for Tab.
//
// The sections keep their own sizes and the meter its width; the row is centred in the body, whatever room is left
// going around it (the layout is provisional, #94). Both views' controls attach to the same host parameters, so an
// edit in one shows in the other.
class CompactView final : public juce::Component
{
public:
    CompactView (PluginProcessor& processor, LevelHistory& history);
    ~CompactView() override;

    GainReductionMeter& gainReductionMeter() { return gainReduction; }

    // The row's size: the sections at their own sizes and the meter, with layout::gap between them.
    int rowWidth();
    int sectionsHeight();

    void resized() override;

private:
    // The sections, left to right.
    std::array<Section*, 3> sections() { return { &style, &gate, &compressor }; }

    StyleSection style;
    ControlSection gate, compressor;
    GainReductionMeter gainReduction;
    std::optional<CompFollowAttachment> compAttachment;
};

} // namespace comp1
