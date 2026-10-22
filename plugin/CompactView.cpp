#include "CompactView.h"

#include "Parameters.h"
#include "Sections.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace layout = staple::tokens::layout;

CompactView::CompactView (PluginProcessor& p, LevelHistory& history)
    : style (p), gate ("Gate", p), compressor ("Compressor", p), output ("Output", p), gainReduction (history)
{
    setName ("Compact View");
    gate.addToggle (parameters::gateId, "Gate", true);
    gate.addKnob (parameters::gateThresholdId, knobSize::medium, "Threshold", ControlSection::Place::underStack);
    addCompKnob (compressor, p, compAttachment);
    output.addKnob (parameters::outputGainId, knobSize::medium, "Output Gain");

    int order = 0;
    for (auto* section : sections())
    {
        addAndMakeVisible (*section);
        section->setExplicitFocusOrder (++order);
    }
    addAndMakeVisible (gainReduction);
}

CompactView::~CompactView() = default;

int CompactView::rowWidth()
{
    int width = layout::meterWidth;
    for (auto* section : sections())
        width += section->idealWidth() + layout::gap;
    return width;
}

int CompactView::sectionsHeight()
{
    return tallestOf (sections());
}

void CompactView::resized()
{
    auto row = getLocalBounds().withSizeKeepingCentre (rowWidth(), getHeight());
    gainReduction.setBounds (row.removeFromRight (layout::meterWidth));
    row.removeFromRight (layout::gap);
    auto line = row.withSizeKeepingCentre (row.getWidth(), sectionsHeight());
    for (auto* section : sections())
    {
        section->setBounds (line.removeFromLeft (section->idealWidth()));
        line.removeFromLeft (layout::gap);
    }
}

} // namespace comp1
