#include "Display.h"

namespace comp1
{

Display::Display (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p), level (history, p), transferCurve (p, history),
      inset (p, history)
{
    setName ("Display");
    level.setName ("Level");
    detectionEq.setName ("Detection EQ");
    int order = 0;
    for (const auto tab : allTabs)
    {
        auto& chip = chipOf (tab);
        chip.setTitle (chip.getButtonText());
        chip.setTooltip ("Show the " + chip.getButtonText() + " display");
        chip.onClick = [this, tab] {
            processor.setDisplayTab (tab);
            show (tab);
        };
        // Left to right.
        chip.setExplicitFocusOrder (++order);
        addAndMakeVisible (chip);
        addChildComponent (tabContent (tab));
    }
    detectionEq.addAndMakeVisible (eqDisplay);
    // After the tabs.
    inset.setExplicitFocusOrder (++order);
    addChildComponent (inset);
    show (processor.displayTab());
}

juce::Component& Display::tabContent (DisplayTab tab)
{
    switch (tab)
    {
        case DisplayTab::level: return level;
        case DisplayTab::detectionEq: return detectionEq;
        case DisplayTab::transferCurve: return transferCurve;
    }
    return level;
}

staple::TextChip& Display::chipOf (DisplayTab tab)
{
    switch (tab)
    {
        case DisplayTab::level: return levelChip;
        case DisplayTab::detectionEq: return detectionEqChip;
        case DisplayTab::transferCurve: return transferCurveChip;
    }
    return levelChip;
}

void Display::tick (double)
{
    if (processor.displayTab() != shown)
        show (processor.displayTab());
}

void Display::show (DisplayTab tab)
{
    shown = tab;
    for (const auto each : allTabs)
    {
        chipOf (each).setToggleState (each == tab, juce::dontSendNotification);
        tabContent (each).setVisible (each == tab);
    }
    inset.setVisible (tab == DisplayTab::level);
    // Detection Audition is held only while its tab shows.
    if (tab != DisplayTab::detectionEq)
        eqDisplay.letGoOfDetectionAudition();
}

void Display::resized()
{
    auto area = getLocalBounds();
    auto tabs = area.removeFromTop (tabHeight);
    area.removeFromTop (contentGap);
    for (const auto tab : allTabs)
    {
        auto& chip = chipOf (tab);
        chip.setBounds (tabs.removeFromLeft (chip.getIdealWidth()));
        tabs.removeFromLeft (tabGap);
        tabContent (tab).setBounds (area);
    }
    eqDisplay.setBounds (detectionEq.getLocalBounds());
    inset.setTopLeftPosition (level.getPosition().translated (insetGap, insetGap));
}

} // namespace comp1
