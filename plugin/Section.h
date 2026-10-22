#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <span>

namespace comp1
{

// One of the editor's sections, in either view: its title in uppercase, letter-spaced, over a 1 px line1 divider, and
// under them its controls, which the section's owner adds and places in controlArea(). Named by its title, as a test
// finds it.
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& title);

    void paint (juce::Graphics& g) override;

    // Where its controls go, under the title and the divider, in its own pixels.
    juce::Rectangle<int> controlArea() const;
    // The width its title takes, uppercase and letter-spaced: the least the section is.
    int titleWidth() const;
    // Its width and height for its title and controls: it keeps these whatever the window's size.
    virtual int idealWidth() const = 0;
    virtual int idealHeight() const = 0;

    static constexpr int titleHeight = 16, dividerGap = 8;
};

// The height of a row of sections: its tallest's.
int tallestOf (std::span<Section* const> row);

} // namespace comp1
