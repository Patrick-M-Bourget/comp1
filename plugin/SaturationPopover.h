#pragma once

#include "PluginProcessor.h"
#include "ReadoutPopover.h"
#include "SectionPopover.h"
#include "Sections.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/FooterReadout.h"

namespace comp1
{

// The Saturation popover the footer's Saturation readout opens (SectionPopover): the Saturation section
// (SaturationSection), Saturation on/off and Saturation Position over Drive. Opened from the keys, focus moves to the
// on switch.
using SaturationPopover = SectionPopover<SaturationSection>;

// The footer's Saturation readout and the Saturation popover it opens: one footer item, in both views. The readout
// (plugkit::ui::FooterReadout, titled "Saturation", shrinking to "SAT") reads "SAT" and Drive in whole dB, signed
// (saturationReadoutText), drawn in text4, not applied, while Saturation is off or Global Bypass on; it follows the
// parameters on the editor's tick. A click on it, or Space or Return, opens and closes the popover, fill2 behind the readout while it is
// open. The readout fills the item.
class SaturationItem final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    explicit SaturationItem (PluginProcessor& processor);

    plugkit::ui::FooterReadout& readout() { return shown; }
    SaturationPopover& popover() { return saturationPopover; }

    // The readout's width for its widest reading, Drive's top, so it keeps its width as Drive changes.
    int idealWidth();
    // The readout's width for its label alone, "SAT": the least the footer gives it.
    int shrunkWidth() const { return shown.getShrunkWidth(); }

    void resized() override;

private:
    void tick (double) override { show(); }
    // Shows Drive, and whether Saturation is on.
    void show();

    PluginProcessor& processor;
    plugkit::ui::FooterReadout shown;
    SaturationPopover saturationPopover;
    ReadoutPopover opens;
};

// What the Saturation readout reads: "SAT" and Drive in whole dB, signed, "SAT +6 dB", "SAT 0 dB".
juce::String saturationReadoutText (double driveDb);

} // namespace comp1
