#pragma once

#include "PluginProcessor.h"
#include "ReadoutPopover.h"
#include "Sections.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/FooterPopoverFrame.h"
#include "plugkit/ui/FooterReadout.h"

namespace comp1
{

// The stereo popover the footer's Stereo readout opens, in both views (plugkit::ui::FooterPopoverFrame): above the
// readout, right-aligned to it, closed by a second click, Escape or a click outside. It holds the Stereo section
// (StereoSection), Stereo Placement then Stereo Link, each attached to its host parameter, so they follow Host
// Automation, undo, A/B Compare and Presets, and each edit is one undo step. Opened from the keys, focus moves to Stereo
// Placement, and Tab reaches Stereo Link; closed, focus inside it goes back to the readout. On a mono track both are
// disabled, their values kept.
class StereoPopover final : public plugkit::ui::FooterPopoverFrame
{
public:
    explicit StereoPopover (PluginProcessor& processor);

    void resized() override;

    static constexpr int padding = 12;

private:
    void followProcessor() override { stereo.followProcessor(); }

    StereoSection stereo;
};

// The footer's Stereo readout and the stereo popover it opens: one footer item, in both views. The readout
// (plugkit::ui::FooterReadout, titled "Stereo", shrinking to "ST") reads Stereo Placement then Stereo Link in whole
// percent (stereoReadoutText), drawn in text4, not applied, while Global Bypass is on, and on a mono track, where neither
// changes anything, which its description tells a screen reader; it follows the processor on the editor's tick. A click on it, or Space or Return,
// opens and closes the popover, fill2 behind the readout while it is open. The readout fills the item.
class StereoItem final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    explicit StereoItem (PluginProcessor& processor);

    plugkit::ui::FooterReadout& readout() { return shown; }
    StereoPopover& popover() { return stereoPopover; }

    // The readout's width for its widest reading, "Stereo 100%", so it keeps its width as its settings change.
    int idealWidth();
    // The readout's width for its label alone, "ST": the least the footer gives it.
    int shrunkWidth() const { return shown.getShrunkWidth(); }

    void resized() override;

private:
    void tick (double) override { show(); }
    // Shows Stereo Placement and Stereo Link, and whether the track is mono.
    void show();

    PluginProcessor& processor;
    plugkit::ui::FooterReadout shown;
    StereoPopover stereoPopover;
    ReadoutPopover opens;
};

// What the Stereo readout reads: Stereo Placement's name, then Stereo Link in whole percent, "Stereo 100%", "Mid 80%".
juce::String stereoReadoutText (const juce::String& placement, double linkPercent);

} // namespace comp1
