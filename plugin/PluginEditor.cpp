#include "PluginEditor.h"

#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace layout = staple::tokens::layout;

PluginEditor::PluginEditor (PluginProcessor& p)
    : EditorShell (p, p.session(), { "comp1", parameters::globalBypassId }), history (*this, p), displaySlot (p, history), gainReduction (history),
      outputMeter (p), gate (p), compressor (p, history), character (p), detection (p), stereo (p),
      lookahead (p), footer (p, history), compactBody (p, history)
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
    for (auto& [meter, label] : labelledMeters())
        content().addAndMakeVisible (*label);
    // Tab's order, after the header: the display's tabs, the Output Meter, then the sections in signal order. The Gain
    // Reduction meters are read, not operated: they are no stops.
    int order = firstFocusOrder;
    for (juce::Component* child : std::initializer_list<juce::Component*> { &displaySlot, &outputMeter, &sections })
    {
        content().addAndMakeVisible (*child);
        child->setExplicitFocusOrder (order++);
    }
    // The Detection section's slide gives the display what it takes, or takes it back, the window keeping its size.
    detection.onHeightChange = [this] {
        if (! bodyArea.isEmpty())
            layOutBody (bodyArea);
    };
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
    for (auto& [meter, label] : labelledMeters())
        label->setVisible (expanded);
    compactBody.setVisible (! expanded);
    footer.showSwitchFor (view);
    if (! expanded)
        detection.letGoOfDetectionAudition();
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

std::array<std::pair<juce::Component*, MeterLabel*>, 4> PluginEditor::labelledMeters()
{
    return { { { &gateGainReduction, &gateLabel }, { &gainReduction, &compLabel }, { &limiterGainReduction, &limiterLabel }, { &outputMeter, &outputLabel } } };
}

std::vector<std::vector<Section*>> PluginEditor::sectionRows()
{
    return { { &gate, &compressor, &character }, { &detection, &stereo, &lookahead } };
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
    bodyArea = area;
    compactBody.setBounds (area);
    sections.setBounds (area.removeFromBottom (sectionRowsHeight()));
    area.removeFromBottom (layout::gap);
    const auto meters = labelledMeters();
    const int count = static_cast<int> (meters.size());
    auto meterArea = area.removeFromRight (count * layout::meterWidth + (count - 1) * layout::gap);
    area.removeFromRight (layout::gap);
    // Each meter's label on the line under it: the meters give up only that line.
    auto labelLine = meterArea.removeFromBottom (MeterLabel::lineHeight);
    for (auto& [meter, label] : meters)
    {
        meter->setBounds (meterArea.removeFromLeft (layout::meterWidth));
        label->setBounds (labelLine.removeFromLeft (layout::meterWidth));
        meterArea.removeFromLeft (layout::gap);
        labelLine.removeFromLeft (layout::gap);
    }
    displaySlot.setBounds (area);

    // The grid's three columns: Gate's, the Compressor's and Character and Saturation's, each as wide as the widest it
    // holds. The second row's Detection spans the first two, and Stereo and Lookahead share the third.
    const int first = gate.idealWidth();
    const int second = std::max (compressor.idealWidth(), detection.idealWidth() - first - sectionGap);
    const int third = std::max (character.idealWidth(), stereo.idealWidth() + sectionGap + lookahead.idealWidth());
    const int secondX = first + sectionGap, thirdX = secondX + second + sectionGap;
    // Each from its row's top at its own height: the Detection section collapsed is a strip.
    const auto place = [] (Section& section, int top, int x, int width) { section.setBounds (x, top, width, section.idealHeight()); };
    place (gate, 0, 0, first);
    place (compressor, 0, secondX, second);
    place (character, 0, thirdX, third);
    const int secondTop = tallestOf (sectionRows().front()) + sectionGap;
    place (detection, secondTop, 0, secondX + second);
    place (stereo, secondTop, thirdX, stereo.idealWidth());
    const int lookaheadX = thirdX + stereo.idealWidth() + sectionGap;
    place (lookahead, secondTop, lookaheadX, thirdX + third - lookaheadX);
}

} // namespace comp1
