#pragma once

#include "HandlePainting.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// A drag handle on one of the display's insets (the Transfer Curve inset's, the Timing Response inset's): a component
// of its own, centred where its inset places it, drawn with the editor's handle look (handles::paint) as its inset
// says, and taking a press only within hitRadius of its centre, so a press beside it passes through. It hands its
// press, drag and release to its inset, and tells it when Alt changes over it. It is hidden from the screen reader
// and takes no focus: its inset is the control.
class DragHandle final : public juce::Component
{
public:
    // What a handle hands its mouse to.
    class Owner
    {
    public:
        virtual ~Owner() = default;
        // How the handle draws now.
        virtual handles::Look lookOf (const DragHandle& handle) const = 0;
        virtual void grab (DragHandle& handle, const juce::MouseEvent& e) = 0;
        virtual void dragTo (const juce::MouseEvent& e) = 0;
        virtual void letGo() = 0;
        // Alt was pressed or let go over it, or the pointer left it with Alt held (altHeld).
        virtual void altChanged (DragHandle&) {}
    };

    DragHandle (Owner& owner, const juce::String& name, float hitRadius, const juce::MouseCursor& cursor);

    bool hovered = false;
    bool altHeld = false;      // Alt, while hovered
    juce::Point<float> centre; // in its inset

    // Centred on at, in its inset.
    void place (juce::Point<float> at);
    juce::Point<float> localCentre() const { return centre - getPosition().toFloat(); }

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override { setHovered (true, e.mods.isAltDown()); }
    void mouseMove (const juce::MouseEvent& e) override { setHovered (true, e.mods.isAltDown()); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (false, false); }
    void modifierKeysChanged (const juce::ModifierKeys& mods) override;
    void mouseDown (const juce::MouseEvent& e) override { owner.grab (*this, e); }
    void mouseDrag (const juce::MouseEvent& e) override { owner.dragTo (e); }
    void mouseUp (const juce::MouseEvent&) override { owner.letGo(); }

private:
    void setHovered (bool now, bool alt);

    Owner& owner;
    const float hitRadius;
};

} // namespace comp1
