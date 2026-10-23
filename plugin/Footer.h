#pragma once

#include "LevelHistory.h"
#include "OutputPopover.h"
#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/OutputReadout.h"
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

// The Auto Gain dot: a dot of diameter, in Auto Gain's on ink (the A toggle's, text1), centred in its bounds, which the
// footer gives it in the gap right of the Output readout. No text; its tooltip reads "Auto Gain: " and the amount. It
// takes no keyboard focus and is hidden from screen readers, as the readout's description says the amount.
class AutoGainDot final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AutoGainDot();

    void paint (juce::Graphics& g) override;

    static constexpr float diameter = 4.0f;
};

// The footer both views share: Global Bypass at the far left, the power button showing Off while on; then Style,
// Processing Mode and Oversampling, as chips that open menus; at the right the Output readout, the UI Scale menu and the
// switch between the views, in that order, which is Tab's order too.
//
// The Style chip reads "Style: " and the Style. Its menu lists the offered Styles (PluginProcessor::offeredStyles) in
// Style's order, each with the Character it plays in brackets, "Drums (FET)", or alone where the Style is that
// Character, "FET"; a screen reader reads that item as its value. A pick chooses the Style
// (PluginProcessor::chooseStyle), so its defaults are written as one undo step; the chip never writes the Style
// parameter itself. Up and down don't step it, as each step would write a Style's defaults. It shows the Style
// parameter, so it follows an undo, a loaded Preset, a restored session and an A/B switch.
//
// The Output readout (plugkit::ui::OutputReadout) reads Output Gain plus Auto Gain's estimate while Auto Gain is on
// (PluginProcessor::appliedAutoGainDb), signed to one decimal; Output Gain's range
// has no silent bottom, so it always reads as a level. While Dry is above Off (parallel compression) a "||" mark leads
// it, in its ink, and a screen reader is told so by its description. It is drawn in text4, not applied, while Global
// Bypass is on. While there is an Auto Gain amount to show (shownAutoGain: one that reads other than zero, with Global
// Bypass off), its description says so, "Includes Auto Gain: -4.2 dB", after the parallel mark's, and the Auto Gain dot
// (AutoGainDot) shows in the gap right of it; the dot's place is kept while it is hidden, so nothing moves.
// A click on it, or Space or Return, opens and closes the output popover (OutputPopover), fill2 behind the readout
// while it is open. It follows the processor on the editor's tick.
//
// Oversampling offers only the factors at or under the cap at the sample rate the processor was prepared at (or the
// assumed one before it is), comp1/Latency.h's maxOversampling, and shows the factor in force, the cap when the
// stored one is over it. In Zero Latency, which has no Oversampling, it is disabled.
class Footer final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    Footer (PluginProcessor& processor, LevelHistory& history);
    ~Footer() override;

    void resized() override;

    plugkit::ui::UiScaleMenu& uiScaleMenu() { return uiScale; }
    plugkit::ui::OutputReadout& outputReadout() { return output; }
    OutputPopover& outputPopover() { return popover; }
    AutoGainDot& autoGainDot() { return dot; }

    // The switch between the views, at the far right after the UI Scale menu, last in Tab's order: Staple icon buttons
    // titled by what they do, the collapse arrow "Switch to the Compact View" in the Expanded View, the expand arrow
    // "Switch to the Expanded View" in the Compact View. A click, or Space or Return, calls onSwitchView.
    juce::Button& viewSwitch() { return toCompact.isVisible() ? toCompact : toExpanded; }
    std::function<void()> onSwitchView;
    // Shows the switch for the view shown, keeping keyboard focus on the switch if it had it.
    void showSwitchFor (View shown);

    // The Output readout is at least outputWidth wide, so it keeps its width as its level changes.
    static constexpr int buttonSize = 28, inset = 6, chipGap = 8, outputWidth = 70;

private:
    void tick (double) override { showOutput(); }
    // Shows the Output readout's level, and whether it is applied.
    void showOutput();

    PluginProcessor& processor;
    staple::IconButton globalBypass { "Global Bypass", staple::Icon::power };
    juce::AudioProcessorValueTreeState::ButtonAttachment globalBypassAttachment;
    ChoiceChip style, processingMode, oversampling;
    plugkit::ui::OutputReadout output;
    AutoGainDot dot;
    OutputPopover popover;
    plugkit::ui::UiScaleMenu uiScale;
    staple::IconButton toCompact { "Switch to the Compact View", staple::Icon::collapse },
        toExpanded { "Switch to the Expanded View", staple::Icon::expand };
};

} // namespace comp1
