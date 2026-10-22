#include "Display.h"

namespace comp1
{

Display::Display (PluginProcessor& p) : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p)
{
    setName ("Display");
    level.setName ("Level");
    detectionEq.setName ("Detection EQ");
    int order = 0;
    for (const auto tab : { DisplayTab::level, DisplayTab::detectionEq })
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
    show (processor.displayTab());
}

void Display::tick (double)
{
    if (processor.displayTab() != shown)
        show (processor.displayTab());
}

void Display::show (DisplayTab tab)
{
    shown = tab;
    for (const auto each : { DisplayTab::level, DisplayTab::detectionEq })
    {
        chipOf (each).setToggleState (each == tab, juce::dontSendNotification);
        tabContent (each).setVisible (each == tab);
    }
}

void Display::resized()
{
    auto area = getLocalBounds();
    auto tabs = area.removeFromTop (tabHeight);
    area.removeFromTop (contentGap);
    for (const auto tab : { DisplayTab::level, DisplayTab::detectionEq })
    {
        auto& chip = chipOf (tab);
        chip.setBounds (tabs.removeFromLeft (chip.getIdealWidth()));
        tabs.removeFromLeft (tabGap);
        tabContent (tab).setBounds (area);
    }
}

} // namespace comp1
