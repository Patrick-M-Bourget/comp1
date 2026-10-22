#include "PluginEditor.h"

#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace layout = staple::tokens::layout;

PluginEditor::PluginEditor (PluginProcessor& p)
    : EditorShell (p, p.session(), { "comp1", parameters::globalBypassId }), history (*this, p), displaySlot (p), gainReduction (history),
      outputMeter (p), styleSection (p), gate (p), compressor (p, history), character (p), dryWet (p), output (p), detection (p), stereo (p),
      lookahead (p), footer (p)
{
    editorTick().add (history);
    sections.setName ("Sections");
    int sectionOrder = 0;
    for (auto& row : sectionRows())
        for (auto* section : row)
        {
            sections.addAndMakeVisible (*section);
            section->setExplicitFocusOrder (++sectionOrder);
        }
    content().addAndMakeVisible (gainReduction);
    // Tab's order, after the header: the display's tabs, the Output Meter, then the sections in signal order. The Gain
    // Reduction meter is read, not operated: it is no stop.
    int order = firstFocusOrder;
    for (juce::Component* child : std::initializer_list<juce::Component*> { &displaySlot, &outputMeter, &sections })
    {
        content().addAndMakeVisible (*child);
        child->setExplicitFocusOrder (order++);
    }
    footer.uiScaleMenu().onPicked = [this] (int) { applyUiScale(); };
    // Along the bottom, last in Tab's order.
    open (footer);
}

PluginEditor::~PluginEditor() = default;

std::vector<std::vector<Section*>> PluginEditor::sectionRows()
{
    return { { &styleSection, &gate, &compressor, &character }, { &dryWet, &output, &detection, &stereo, &lookahead } };
}

int PluginEditor::widthOf (Section& section)
{
    if (auto* controls = dynamic_cast<ControlSection*> (&section))
        return controls->idealWidth();
    return styleSectionWidth;
}

int PluginEditor::heightOf (Section& section)
{
    if (auto* controls = dynamic_cast<ControlSection*> (&section))
        return controls->idealHeight();
    return 0;
}

int PluginEditor::sectionRowsHeight()
{
    int height = 0;
    for (auto& row : sectionRows())
    {
        int rowHeight = 0;
        for (auto* section : row)
            rowHeight = std::max (rowHeight, heightOf (*section));
        height += rowHeight + sectionGap;
    }
    return height - sectionGap;
}

void PluginEditor::layOutBody (juce::Rectangle<int> area)
{
    sections.setBounds (area.removeFromBottom (sectionRowsHeight()));
    area.removeFromBottom (layout::gap);
    outputMeter.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    gainReduction.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    displaySlot.setBounds (area);

    auto rows = sections.getLocalBounds();
    for (auto& row : sectionRows())
    {
        int rowHeight = 0;
        for (auto* section : row)
            rowHeight = std::max (rowHeight, heightOf (*section));
        auto line = rows.removeFromTop (rowHeight);
        rows.removeFromTop (sectionGap);
        for (auto* section : row)
        {
            section->setBounds (line.removeFromLeft (widthOf (*section)));
            line.removeFromLeft (sectionGap);
        }
    }
}

} // namespace comp1
