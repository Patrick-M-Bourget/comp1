#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace comp1
{

// The edit half the display's insets share (the Transfer Curve inset's, the Timing Response inset's), beside the drag
// handles (DragHandle) they move their settings with.
//
// - **One undo step:** a drag, or an arrow key's press with its repeats, is one edit, which the inset opens and
//   closeEdit closes: at the drag's release, the key's release or the inset losing focus, whichever ends the last of
//   them.
// - **Drags:** Shift drags finely, by the knobs' proportion (staple::tokens::knob::fineDragPixels); Shift pressed or let
//   go mid-drag carries on from where the setting is, at the new speed, the inset anchoring the drag again.
// - **Keys:** an arrow steps a setting by 1% of its parameter's travel, 0.2% with Shift, as the knobs' arrows do
//   (KeyboardSlider): right and up raise it, left and down lower it.
// - **Screen reader:** the inset's value is announced when it changes.
class InsetEdit
{
public:
    explicit InsetEdit (std::function<void()> closeEdit);

    // An arrow key: across (left, right) or up and down, the way it steps, and how far in a parameter's travel.
    struct Arrow
    {
        bool horizontal = false;
        int direction = 0;
        double step = 0.0;
    };
    // The arrow key pressed; none for any other key.
    static std::optional<Arrow> arrowOf (const juce::KeyPress& key);
    // An arrow key stepped a setting, inside the edit the inset opened: the edit stays open until the key's release.
    void keyHeld() { heldKey = true; }
    void keyStateChanged (bool isKeyDown);
    void focusLost() { endHeldKey(); }

    // A drag began, the inset's edit open; Shift in mods drags finely.
    void grab (const juce::ModifierKeys& mods);
    // Whether Shift was pressed or let go since the drag was anchored: if so the inset anchors it again where the setting
    // is, and the drag carries on at the new speed.
    bool shiftChanged (const juce::ModifierKeys& mods);
    // How far a drag moves its setting, against a plain drag's: 1, or less with Shift.
    double dragSpeed() const;
    // The drag ended: closes the edit, unless an arrow key still holds it open.
    void letGo();

    // Tells a screen reader inset's value changed, when value isn't what it last told.
    void announce (juce::Component& inset, const juce::String& value);

private:
    void endHeldKey();

    std::function<void()> closeEdit;
    bool heldKey = false, dragging = false, fine = false;
    juce::String announced;
};

} // namespace comp1
