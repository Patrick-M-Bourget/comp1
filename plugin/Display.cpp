#include "Display.h"

namespace comp1
{

Display::Display (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p), level (history, p), inset (p, history), timing (p), spectrum (p)
{
    setName ("Display");
    level.setName ("Level");
    int order = 0;
    for (auto& [tab, chip, content] : tabs)
    {
        chip.setTitle (chip.getButtonText());
        chip.setTooltip ("Show the " + chip.getButtonText() + " display");
        chip.onClick = [this, tab] {
            processor.setDisplayTab (tab);
            show (tab);
        };
        // Left to right.
        chip.setExplicitFocusOrder (++order);
        addAndMakeVisible (chip);
        addChildComponent (content);
    }
    // After the tabs.
    inset.setExplicitFocusOrder (++order);
    addChildComponent (inset);
    timing.setExplicitFocusOrder (++order);
    addChildComponent (timing);
    show (processor.displayTab());
}

juce::Component& Display::tabContent (DisplayTab tab)
{
    return tabs[rowOf (tab)].content;
}

void Display::tick (double)
{
    if (processor.displayTab() != shown)
        show (processor.displayTab());
}

void Display::show (DisplayTab tab)
{
    shown = tab;
    for (auto& [rowTab, chip, content] : tabs)
    {
        chip.setToggleState (rowTab == tab, juce::dontSendNotification);
        content.setVisible (rowTab == tab);
    }
    inset.setVisible (tab == DisplayTab::level);
    timing.setVisible (tab == DisplayTab::level);
}

void Display::resized()
{
    auto area = getLocalBounds();
    auto chips = area.removeFromTop (tabHeight);
    area.removeFromTop (contentGap);
    for (auto& [tab, chip, content] : tabs)
    {
        chip.setBounds (chips.removeFromLeft (chip.getIdealWidth()));
        chips.removeFromLeft (tabGap);
        content.setBounds (area);
    }
    inset.setTopLeftPosition (level.getPosition().translated (insetGap, insetGap));
    timing.setTopLeftPosition (inset.getPosition().translated (inset.getWidth() + insetGap, 0));
}

} // namespace comp1
