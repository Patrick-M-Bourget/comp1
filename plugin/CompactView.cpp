#include "CompactView.h"

#include "Parameters.h"
#include "Sections.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace layout = staple::tokens::layout;

CompactView::CompactView (PluginProcessor& p, LevelHistory& history)
    : gate ("Gate", p), compressor ("Compressor", p), gainReduction (history)
{
    setName ("Compact View");
    gate.addToggle (parameters::gateId, "Gate", true);
    gate.addKnob (parameters::gateThresholdId, knobSize::medium, "Threshold", ControlSection::Place::underStack);
    addCompKnob (compressor, p, compAttachment);

    int order = 0;
    for (auto* section : sections())
    {
        addAndMakeVisible (*section);
        section->setExplicitFocusOrder (++order);
    }
    addAndMakeVisible (gainReduction);
}

CompactView::~CompactView() = default;

int CompactView::sectionsHeight()
{
    return tallestOf (sections());
}

void CompactView::resized()
{
    auto row = getLocalBounds();
    gainReduction.setBounds (row.removeFromRight (layout::meterWidth));
    const int top = (getHeight() - sectionsHeight()) / 2;
    gate.setBounds (row.getX(), top, gate.idealWidth(), sectionsHeight());
    // Comp centred in the room between Gate and the meter.
    const auto between = row.withLeft (gate.getRight() + layout::gap).withTrimmedRight (layout::gap);
    compressor.setBounds (between.getCentreX() - compressor.idealWidth() / 2, top, compressor.idealWidth(), sectionsHeight());
}

} // namespace comp1
