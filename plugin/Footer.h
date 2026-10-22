#pragma once

#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/Session.h"
#include "plugkit/ui/UiScaleMenu.h"
#include "plugkit/ui/staple/controls/IconButton.h"
#include "plugkit/ui/staple/controls/TextChip.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace comp1
{

// A choice host parameter as a text chip showing its choice, which opens a menu of the choices offered, the one shown
// ticked: a pick writes it inside a gesture, one undo step. Up and down (left and right too) step it to the previous
// or next choice offered, one undo step each; Space and Return open the menu (plugkit::ui::KeyboardControl). It shows,
// on the editor's tick, the choice shown() gives and is enabled while available() is true. Titled with its
// parameter's name, and read with its choice as its value.
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
    };

    ChoiceChip (juce::RangedAudioParameter& parameter, Config config);

    // The menu a click opens, each choice offered an item that picks it.
    juce::PopupMenu menu();
    // Writes choice, inside a gesture: one undo step.
    void pick (int choice);

    // As wide as its longest choice needs, so it keeps its width whichever it shows.
    int widestIdealWidth();

    bool keyPressed (const juce::KeyPress& key) override;

private:
    void tick (double) override { show(); }
    void show();
    std::vector<int> offered() const;
    int shownChoice() const;

    juce::RangedAudioParameter& parameter;
    const juce::StringArray choices;
    Config config;
};

// The footer both views share: Global Bypass at the far left, the power button showing Off while on; then Processing
// Mode and Oversampling, as chips that open menus; the UI Scale menu at the right; and the switch between the views.
//
// Oversampling offers only the factors at or under the cap at the sample rate the processor was prepared at (or the
// assumed one before it is), comp1/Latency.h's maxOversampling, and shows the factor in force, the cap when the
// stored one is over it. In Zero Latency, which has no Oversampling, it is disabled.
class Footer final : public juce::Component
{
public:
    explicit Footer (PluginProcessor& processor);

    void resized() override;

    plugkit::ui::UiScaleMenu& uiScaleMenu() { return uiScale; }

    // The switch between the views, at the far right after the UI Scale menu, last in Tab's order: Staple icon buttons
    // titled by what they do, the collapse arrow "Switch to the Compact View" in the Expanded View, the expand arrow
    // "Switch to the Expanded View" in the Compact View. A click, or Space or Return, calls onSwitchView.
    juce::Button& viewSwitch() { return toCompact.isVisible() ? toCompact : toExpanded; }
    std::function<void()> onSwitchView;
    // Shows the switch for the view shown, keeping keyboard focus on the switch if it had it.
    void showSwitchFor (View shown);

    static constexpr int buttonSize = 28, inset = 6, chipGap = 8;

private:
    staple::IconButton globalBypass { "Global Bypass", staple::Icon::power };
    juce::AudioProcessorValueTreeState::ButtonAttachment globalBypassAttachment;
    ChoiceChip processingMode, oversampling;
    plugkit::ui::UiScaleMenu uiScale;
    staple::IconButton toCompact { "Switch to the Compact View", staple::Icon::collapse },
        toExpanded { "Switch to the Expanded View", staple::Icon::expand };
};

} // namespace comp1
