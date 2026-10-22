#include "Display.h"

namespace comp1
{

Display::Display()
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
            if (onTabPicked != nullptr)
                onTabPicked (tab);
        };
        // Left to right.
        chip.setExplicitFocusOrder (++order);
        addAndMakeVisible (chip);
        addChildComponent (tabContent (tab));
    }
    show (DisplayTab::level);
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
