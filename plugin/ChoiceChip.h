#pragma once

#include "plugkit/ui/EditHistory.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/controls/TextChip.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace comp1
{

// A choice host parameter's choice: the index its value stands for.
int choiceOf (const juce::RangedAudioParameter& parameter);

// A choice host parameter as a text chip showing its choice, which opens a menu of the choices offered, the one shown
// ticked: a pick chooses it, by default writing it inside a gesture, one undo step. Unless told not to step, up and
// down step it to the previous or next choice offered, a held key with its repeats being one undo step, ended by the
// key's release or the chip losing focus, as plugkit::ui::KeyboardControl steps a ComboBox; Space and Return open the
// menu (KeyboardControl). It shows, on the editor's tick, the choice shown() gives and is enabled while available() is
// true. Titled with its parameter's name, and read with the shown choice's menu item as its value.
class ChoiceChip final : public staple::TextChip, private plugkit::ui::EditorTick::Client
{
public:
    struct Config
    {
        // The choices' indices it offers, in order; all of them unless given.
        std::function<std::vector<int>()> offered;
        // The choice it shows; the parameter's unless given.
        std::function<int()> shown;
        // Whether it is enabled; always unless given.
        std::function<bool()> available;
        // Its text for a choice; the choice's name unless given.
        std::function<juce::String (int)> text;
        // A choice's menu item, which a screen reader reads as its value while it shows that choice; the choice's name
        // unless given.
        std::function<juce::String (int)> item;
        // What a pick does; unless given, writes the choice inside a gesture: one undo step.
        std::function<void (int)> choose;
        // Whether up and down step it. Space and Return open its menu either way.
        bool steps = true;
    };

    ChoiceChip (juce::RangedAudioParameter& parameter, plugkit::ui::EditHistory& history, Config config);
    ~ChoiceChip() override;

    // The menu a click opens, each choice offered an item that picks it.
    juce::PopupMenu menu();
    // Picks choice, as its menu item does: choose() it, or write it inside a gesture, one undo step.
    void pick (int choice);

    // As wide as its longest choice's text needs, so it keeps its width whichever it shows.
    int widestIdealWidth();

    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType cause) override;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void tick (double) override { show(); }
    void show();
    juce::String textOf (int choice) const;
    juce::String itemOf (int choice) const;
    // Ends a held key's undo step, if one is open.
    void endStep();
    std::vector<int> offered() const;
    int shownChoice() const;

    juce::RangedAudioParameter& parameter;
    plugkit::ui::EditHistory& history;
    const juce::StringArray choices;
    Config config;
    bool stepping = false; // a held key's undo step is open
};

} // namespace comp1
