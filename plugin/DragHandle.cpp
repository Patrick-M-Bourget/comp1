#include "DragHandle.h"

#include <utility>

namespace comp1
{

DragHandle::DragHandle (Owner& o, const juce::String& name, float radius, const juce::MouseCursor& cursor) : owner (o), hitRadius (radius)
{
    setName (name);
    setAccessible (false);
    setWantsKeyboardFocus (false);
    setMouseCursor (cursor);
}

void DragHandle::place (juce::Point<float> at)
{
    if (at == centre && ! getBounds().isEmpty())
        return;
    centre = at;
    const float half = hitRadius + 1.0f;
    setBounds (juce::Rectangle<float> (2.0f * half, 2.0f * half).withCentre (at).getSmallestIntegerContainer());
    repaint();
}

bool DragHandle::hitTest (int x, int y)
{
    return juce::Point<float> (static_cast<float> (x) + 0.5f, static_cast<float> (y) + 0.5f).getDistanceFrom (localCentre()) <= hitRadius;
}

void DragHandle::paint (juce::Graphics& g) { handles::paint (g, localCentre(), owner.lookOf (*this)); }

void DragHandle::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    if (hovered)
        setHovered (true, mods.isAltDown());
    // Passed on, so the inset's ancestors still hear it.
    juce::Component::modifierKeysChanged (mods);
}

void DragHandle::setHovered (bool now, bool alt)
{
    if (std::exchange (hovered, now) != now)
        repaint();
    if (std::exchange (altHeld, alt) != alt)
        owner.altChanged (*this);
}

} // namespace comp1
