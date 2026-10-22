#include "DisplayFades.h"

#include "plugkit/ui/staple/WindowBackground.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace comp1
{

namespace
{
namespace display = plugkit::ui::display;

// The display's edge fades, as plugkit's Staple design system gives them (its design-system README.md, "The display").
constexpr display::EdgeFades edgeFades { .top = 18.0f, .bottom = 84.0f, .left = 36.0f, .right = 56.0f };

// The component the editor shell draws the window's background over, which holds everything the editor shows.
const juce::Component* windowOf (const juce::Component& component)
{
    for (auto* c = component.getParentComponent(); c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<const juce::AudioProcessorEditor*> (c->getParentComponent()) != nullptr)
            return c;
    return nullptr;
}
} // namespace

juce::Point<float> DisplayFades::originInWindow (const juce::Component& graph)
{
    const auto* window = windowOf (graph);
    return window != nullptr ? window->getLocalPoint (&graph, juce::Point<float>()) : juce::Point<float>();
}

void DisplayFades::paint (juce::Graphics& g, const juce::Component& graph)
{
    const auto* window = windowOf (graph);
    if (window == nullptr)
        return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto at = window->getLocalPoint (&graph, juce::Point<float>());
    const auto area = juce::Rectangle<float> (at.x, at.y, static_cast<float> (graph.getWidth()), static_cast<float> (graph.getHeight()));
    if (area != madeFor || ! juce::exactlyEqual (scale, fades.scale))
    {
        madeFor = area;
        const auto windowArea = window->getLocalBounds().toFloat();
        fades = display::edgeFadeOverlay ({ .width = graph.getWidth(), .height = graph.getHeight() }, edgeFades, scale, [&] (juce::Graphics& behind) {
            behind.addTransform (juce::AffineTransform::translation (-at.x, -at.y));
            staple::paintWindowBackground (behind, windowArea);
        });
    }
    display::paintEdgeFades (g, fades);
}

} // namespace comp1
