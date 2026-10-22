#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// One of the Expanded View's sections: its title in uppercase, letter-spaced, over a 1 px line1 divider, and under
// them its controls, which the section's owner adds and places in controlArea(). Named by its title, as a test finds
// it.
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& title);

    void paint (juce::Graphics& g) override;

    // Where its controls go, under the title and the divider, in its own pixels.
    juce::Rectangle<int> controlArea() const;

    static constexpr int titleHeight = 16, dividerGap = 8;
};

} // namespace comp1
