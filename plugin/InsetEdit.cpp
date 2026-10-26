#include "InsetEdit.h"

#include "plugkit/ui/staple/Tokens.h"

#include <utility>

namespace comp1
{

namespace
{
// An arrow's step, in a parameter's travel, as the knobs' (KeyboardSlider): 1%, or 0.2% with Shift.
constexpr double keyStep = 0.01, fineKeyStep = 0.002;
} // namespace

InsetEdit::InsetEdit (std::function<void()> close) : closeEdit (std::move (close)) {}

std::optional<InsetEdit::Arrow> InsetEdit::arrowOf (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    if (! horizontal && ! vertical)
        return std::nullopt;
    return Arrow { .horizontal = horizontal,
                   .direction = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1 : -1,
                   .step = key.getModifiers().isShiftDown() ? fineKeyStep : keyStep };
}

void InsetEdit::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        endHeldKey();
}

void InsetEdit::endHeldKey()
{
    if (std::exchange (heldKey, false) && ! dragging)
        closeEdit();
}

void InsetEdit::grab (const juce::ModifierKeys& mods)
{
    dragging = true;
    fine = mods.isShiftDown();
}

bool InsetEdit::shiftChanged (const juce::ModifierKeys& mods)
{
    if (mods.isShiftDown() == fine)
        return false;
    fine = mods.isShiftDown();
    return true;
}

double InsetEdit::dragSpeed() const
{
    namespace knob = staple::tokens::knob;
    return fine ? knob::dragPixels / knob::fineDragPixels : 1.0;
}

void InsetEdit::letGo()
{
    if (! std::exchange (dragging, false))
        return;
    if (! heldKey)
        closeEdit();
}

void InsetEdit::announce (juce::Component& inset, const juce::String& value)
{
    if (value == announced)
        return;
    announced = value;
    if (auto* handler = inset.getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

} // namespace comp1
