#pragma once

#include "PluginProcessor.h"
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

// The footer's Stereo readout, in both views (plugkit::ui::FooterReadout), titled "Stereo" and shrinking to "ST": Stereo
// Placement's name then Stereo Link in whole percent, "Stereo 100%", "Mid 80%". It is drawn not applied (text4) on a
// mono track, where neither changes anything, and a screen reader is told so by its description. A click on it, or
// Space or Return, opens and closes its stereo popover (popover()), which the footer it is in holds hidden while
// closed, so it stays on the editor's tick. It follows the processor on the editor's tick.
class StereoReadout final : public plugkit::ui::FooterReadout, private plugkit::ui::EditorTick::Client
{
public:
    explicit StereoReadout (PluginProcessor& processor);
    ~StereoReadout() override;

    StereoPopover& popover() { return stereoPopover; }
    // As wide as its widest text, "Stereo 100%", needs, so it keeps its width as its settings change.
    int widestIdealWidth();

private:
    void tick (double) override { show(); }
    void show();

    PluginProcessor& processor;
    StereoPopover stereoPopover;
};

} // namespace comp1
