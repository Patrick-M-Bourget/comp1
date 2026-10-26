#pragma once

#include "ChoiceChip.h"
#include "HighQualityPopover.h"
#include "LevelHistory.h"
#include "OutputPopover.h"
#include "PluginProcessor.h"
#include "SaturationPopover.h"
#include "StereoPopover.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/Session.h"
#include "plugkit/ui/UiScaleMenu.h"
#include "plugkit/ui/staple/controls/IconButton.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace comp1
{

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

// The footer both views share: Global Bypass at the far left, the power button showing Off while on; then Style and
// Processing Mode, as chips that open menus, and the High Quality readout (HighQualityItem); at the right the Stereo
// readout (StereoItem), the Saturation readout (SaturationItem), the Output readout (OutputItem), the UI Scale menu and
// the switch between the views, in that order, which is Tab's order too. Each readout opens its popover
// (FooterItem). Where the chips leave the row too little room for the Stereo and
// the Saturation readouts' widest texts, the Stereo readout shrinks to its label, "ST", and where that is not enough,
// the Saturation readout to "SAT" too; neither is ever hidden.
//
// The Style chip reads "Style: " and the Style. Its menu lists the offered Styles (PluginProcessor::offeredStyles) in
// Style's order, each with the Character it plays in brackets, "Drums (FET)", or alone where the Style is that
// Character, "FET"; a screen reader reads that item as its value. A pick chooses the Style
// (PluginProcessor::chooseStyle), so its defaults are written as one undo step; the chip never writes the Style
// parameter itself. Up and down don't step it, as each step would write a Style's defaults. It shows the Style
// parameter, so it follows an undo, a loaded Preset, a restored session and an A/B switch.
//
// The Output readout (outputReading) is at least outputWidth wide, and wider as what it reads needs. While there is an
// Auto Gain amount to show (shownAutoGain: one that reads other than zero, with Global Bypass off), the Auto Gain dot
// (AutoGainDot) shows in the gap right of it; the dot's place is kept while it is hidden, so nothing moves. Both follow
// the processor on the editor's tick.
class Footer final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    Footer (PluginProcessor& processor, LevelHistory& history);
    ~Footer() override;

    void resized() override;

    plugkit::ui::UiScaleMenu& uiScaleMenu() { return uiScale; }
    OutputItem& output() { return outputItem; }
    HighQualityItem& highQuality() { return highQualityItem; }
    SaturationItem& saturation() { return saturationItem; }
    StereoItem& stereo() { return stereoItem; }
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
    void tick (double) override;

    PluginProcessor& processor;
    staple::IconButton globalBypass { "Global Bypass", staple::Icon::power };
    juce::AudioProcessorValueTreeState::ButtonAttachment globalBypassAttachment;
    ChoiceChip style, processingMode;
    HighQualityItem highQualityItem;
    StereoItem stereoItem;
    SaturationItem saturationItem;
    OutputItem outputItem;
    AutoGainDot dot;
    plugkit::ui::UiScaleMenu uiScale;
    staple::IconButton toCompact { "Switch to the Compact View", staple::Icon::collapse },
        toExpanded { "Switch to the Expanded View", staple::Icon::expand };
};

} // namespace comp1
