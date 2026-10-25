#pragma once

#include "ChoiceChip.h"
#include "HighQualityPopover.h"
#include "LevelHistory.h"
#include "OutputPopover.h"
#include "PluginProcessor.h"
#include "SaturationPopover.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/OutputReadout.h"
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
// Processing Mode, as chips that open menus, and the High Quality readout; at the right the Saturation readout
// (SaturationItem), the Output readout, the UI Scale menu and the switch between the views, in that order, which is
// Tab's order too.
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
// The High Quality readout (HighQualityReadout) reads the Oversampling in force. A click on it, or Space or Return, opens
// and closes the High Quality popover (HighQualityPopover), which holds Lookahead and Oversampling, fill2 behind the
// readout while it is open.
class Footer final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    Footer (PluginProcessor& processor, LevelHistory& history);
    ~Footer() override;

    void resized() override;

    plugkit::ui::UiScaleMenu& uiScaleMenu() { return uiScale; }
    plugkit::ui::OutputReadout& outputReadout() { return output; }
    OutputPopover& outputPopover() { return popover; }
    HighQualityReadout& highQualityReadout() { return highQuality; }
    HighQualityPopover& highQualityPopover() { return highQualityPopup; }
    AutoGainDot& autoGainDot() { return dot; }
    SaturationItem& saturation() { return saturationItem; }

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
    ChoiceChip style, processingMode;
    HighQualityReadout highQuality;
    SaturationItem saturationItem;
    plugkit::ui::OutputReadout output;
    AutoGainDot dot;
    OutputPopover popover;
    HighQualityPopover highQualityPopup;
    plugkit::ui::UiScaleMenu uiScale;
    staple::IconButton toCompact { "Switch to the Compact View", staple::Icon::collapse },
        toExpanded { "Switch to the Expanded View", staple::Icon::expand };
};

} // namespace comp1
