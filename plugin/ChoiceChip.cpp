#include "ChoiceChip.h"

#include "plugkit/ui/Accessibility.h"
#include "plugkit/ui/ShowMenu.h"
#include "plugkit/ui/staple/LookAndFeel.h"

#include <algorithm>
#include <utility>

namespace comp1
{

int choiceOf (const juce::RangedAudioParameter& parameter) { return juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue())); }

ChoiceChip::ChoiceChip (juce::RangedAudioParameter& p, plugkit::ui::EditHistory& h, Config c)
    : staple::TextChip ({}, Look::plain), plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), parameter (p), history (h),
      choices (p.getAllValueStrings()), config (std::move (c))
{
    setName (parameter.getName (100));
    setTitle (parameter.getName (100));
    setTooltip (parameter.getName (100));
    setChevron (true);
    onClick = [this] {
        plugkit::ui::showMenu (menu(), *this, juce::PopupMenu::Options().withTargetComponent (this));
    };
    show();
}

ChoiceChip::~ChoiceChip() { endStep(); }

std::vector<int> ChoiceChip::offered() const
{
    if (config.offered != nullptr)
        return config.offered();
    std::vector<int> all (static_cast<size_t> (choices.size()));
    for (int i = 0; i < choices.size(); ++i)
        all[static_cast<size_t> (i)] = i;
    return all;
}

int ChoiceChip::shownChoice() const { return config.shown != nullptr ? config.shown() : choiceOf (parameter); }

juce::String ChoiceChip::textOf (int choice) const { return config.text != nullptr ? config.text (choice) : choices[choice]; }

juce::String ChoiceChip::itemOf (int choice) const { return config.item != nullptr ? config.item (choice) : choices[choice]; }

std::unique_ptr<juce::AccessibilityHandler> ChoiceChip::createAccessibilityHandler()
{
    return plugkit::ui::accessibility::handler (*this, juce::AccessibilityRole::button, [this] { return itemOf (shownChoice()); }, [this] { triggerClick(); });
}

juce::PopupMenu ChoiceChip::menu()
{
    juce::PopupMenu popup;
    const int shown = shownChoice();
    for (const int choice : offered())
        popup.addItem (itemOf (choice), true, choice == shown, [this, choice] { pick (choice); });
    return popup;
}

void ChoiceChip::pick (int choice)
{
    if (config.choose != nullptr)
    {
        config.choose (choice);
        show();
        return;
    }
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (static_cast<float> (choice)));
    parameter.endChangeGesture();
    show();
}

bool ChoiceChip::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const int direction = code == juce::KeyPress::downKey ? 1 : code == juce::KeyPress::upKey ? -1 : 0;
    if (direction == 0 || ! config.steps || key.getModifiers().isAnyModifierKeyDown() || ! isEnabled())
        return staple::TextChip::keyPressed (key);
    staple::LookAndFeel::keyUsed (*this);
    const auto choicesOffered = offered();
    const auto at = std::find (choicesOffered.begin(), choicesOffered.end(), shownChoice());
    const auto index = (at == choicesOffered.end() ? 0 : static_cast<int> (at - choicesOffered.begin())) + direction;
    if (index >= 0 && index < static_cast<int> (choicesOffered.size()))
    {
        if (! std::exchange (stepping, true))
            history.beginTransaction();
        pick (choicesOffered[static_cast<size_t> (index)]);
    }
    return true;
}

bool ChoiceChip::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        endStep();
    return staple::TextChip::keyStateChanged (isKeyDown);
}

void ChoiceChip::focusLost (FocusChangeType cause)
{
    endStep();
    staple::TextChip::focusLost (cause);
}

void ChoiceChip::endStep()
{
    if (std::exchange (stepping, false))
        history.endTransaction();
}

int ChoiceChip::widestIdealWidth()
{
    const auto shown = getButtonText();
    int widest = 0;
    for (int choice = 0; choice < choices.size(); ++choice)
    {
        setButtonText (textOf (choice));
        widest = std::max (widest, getIdealWidth());
    }
    setButtonText (shown);
    return widest;
}

void ChoiceChip::show()
{
    const auto text = textOf (shownChoice());
    if (getButtonText() != text)
        setButtonText (text);
    const bool available = config.available == nullptr || config.available();
    if (isEnabled() != available)
        setEnabled (available);
}

} // namespace comp1
