#pragma once

#include "plugkit/ui/display/EdgeFadeLayer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// The Expanded View's display edges, which dissolve into the window's background over the design system's display
// fades (plugkit's Staple design-system README.md, "The display"): what each of the display's graphs paints over its
// content. The fades are made again only when the graph's size, its place in the window or the scale it is drawn at
// changes.
class DisplayFades
{
public:
    // Paints the fades over graph, from the window's background behind it; nothing while it isn't in a window.
    void paint (juce::Graphics& g, const juce::Component& graph);

    // Where graph's top left is in the window the editor shell draws the background over (the origin plugkit-ui's
    // CachedLabels takes); 0, 0 while it isn't in one.
    static juce::Point<float> originInWindow (const juce::Component& graph);

private:
    plugkit::ui::display::EdgeFadeOverlay fades;
    juce::Rectangle<float> madeFor;
};

} // namespace comp1
