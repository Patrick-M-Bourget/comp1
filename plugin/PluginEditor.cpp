#include "PluginEditor.h"

#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace layout = staple::tokens::layout;

PluginEditor::PluginEditor (PluginProcessor& p)
    : EditorShell (p, p.session(), { "comp1", parameters::globalBypassId }), displaySlot (p), outputMeter (p), styleSection (p), footer (p.parameterState(), p.session())
{
    sections.setName ("Sections");
    sections.addAndMakeVisible (styleSection);
    // Tab's order, after the header: the display's tabs, the Output Meter, then the sections in signal order.
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

void PluginEditor::layOutBody (juce::Rectangle<int> area)
{
    sections.setBounds (area.removeFromBottom (sectionRowHeight));
    area.removeFromBottom (layout::gap);
    outputMeter.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    displaySlot.setBounds (area);

    auto row = sections.getLocalBounds();
    styleSection.setBounds (row.removeFromLeft (styleSectionWidth));
}

} // namespace comp1
