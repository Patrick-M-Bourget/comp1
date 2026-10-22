#include "PluginEditor.h"

#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace layout = staple::tokens::layout;

PluginEditor::PluginEditor (PluginProcessor& p)
    : EditorShell (p, p.session(), { "comp1", parameters::globalBypassId }), history (*this, p), displaySlot (p, history), gainReduction (history),
      outputMeter (p), styleSection (p), gate (p), compressor (p, history), character (p), dryWet (p), output (p, history), detection (p), stereo (p),
      lookahead (p), footer (p), compactBody (p, history)
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
    content().addAndMakeVisible (gateGainReduction);
    content().addAndMakeVisible (limiterGainReduction);
    // Tab's order, after the header: the display's tabs, the Output Meter, then the sections in signal order. The Gain
    // Reduction meters are read, not operated: they are no stops.
    int order = firstFocusOrder;
    for (juce::Component* child : std::initializer_list<juce::Component*> { &displaySlot, &outputMeter, &sections })
    {
        content().addAndMakeVisible (*child);
        child->setExplicitFocusOrder (order++);
    }
    footer.uiScaleMenu().onPicked = [this] (int) {
        applyUiScale();
        applyViewWindow();
    };
    content().addChildComponent (compactBody);
    compactBody.setExplicitFocusOrder (order);
    footer.onSwitchView = [this, &p] {
        p.setView (shown == View::expanded ? View::compact : View::expanded);
        followView();
    };
    // Along the bottom, last in Tab's order.
    open (footer);
    followView();
}

void PluginEditor::followView()
{
    const auto view = static_cast<PluginProcessor&> (processor).view();
    if (view != shown || compactBody.isVisible() != (view == View::compact))
        show (view);
    applyViewWindow();
}

void PluginEditor::show (View view)
{
    shown = view;
    const bool expanded = view == View::expanded;
    for (juce::Component* part :
         std::initializer_list<juce::Component*> { &displaySlot, &gateGainReduction, &gainReduction, &limiterGainReduction, &outputMeter, &sections })
        part->setVisible (expanded);
    compactBody.setVisible (! expanded);
    footer.showSwitchFor (view);
    if (! expanded)
        displaySlot.detectionEqDisplay().letGoOfDetectionAudition();
}

void PluginEditor::applyViewWindow()
{
    const auto& [defaultSize, minimum, maximum] = PluginProcessor::windowOf (shown);
    auto& session = static_cast<PluginProcessor&> (processor).session();
    const float scale = static_cast<float> (session.uiScalePercent()) / 100.0f;
    const auto scaled = [scale] (int logical) { return juce::roundToInt (static_cast<float> (logical) * scale); };
    if (const auto* limits = getConstrainer();
        limits != nullptr && limits->getMinimumWidth() == scaled (minimum.x) && limits->getMinimumHeight() == scaled (minimum.y)
        && limits->getMaximumWidth() == scaled (maximum.x) && limits->getMaximumHeight() == scaled (maximum.y))
        return;
    // Taken before the new limits can resize the window on the way to its size.
    const auto stored = session.editorSize();
    const juce::Point<int> size { juce::jlimit (minimum.x, maximum.x, stored.x), juce::jlimit (minimum.y, maximum.y, stored.y) };
    setResizeLimits (scaled (minimum.x), scaled (minimum.y), scaled (maximum.x), scaled (maximum.y));
    // The resize writes the Session's size (EditorShell::resized), over whatever the new limits wrote on the way.
    setSize (scaled (size.x), scaled (size.y));
}

PluginEditor::~PluginEditor() = default;

std::vector<std::vector<Section*>> PluginEditor::sectionRows()
{
    return { { &styleSection, &gate, &compressor, &character }, { &dryWet, &output, &detection, &stereo, &lookahead } };
}

int PluginEditor::sectionRowsHeight()
{
    int height = 0;
    for (auto& row : sectionRows())
        height += tallestOf (row) + sectionGap;
    return height - sectionGap;
}

void PluginEditor::layOutBody (juce::Rectangle<int> area)
{
    compactBody.setBounds (area);
    sections.setBounds (area.removeFromBottom (sectionRowsHeight()));
    area.removeFromBottom (layout::gap);
    outputMeter.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    limiterGainReduction.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    gainReduction.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    gateGainReduction.setBounds (area.removeFromRight (layout::meterWidth));
    area.removeFromRight (layout::gap);
    displaySlot.setBounds (area);

    auto rows = sections.getLocalBounds();
    for (auto& row : sectionRows())
    {
        auto line = rows.removeFromTop (tallestOf (row));
        rows.removeFromTop (sectionGap);
        for (auto* section : row)
        {
            section->setBounds (line.removeFromLeft (section->idealWidth()));
            line.removeFromLeft (sectionGap);
        }
    }
}

} // namespace comp1
