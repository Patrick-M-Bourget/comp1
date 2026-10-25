#pragma once

#include "plugkit/ui/FooterPopoverFrame.h"
#include "plugkit/ui/FooterReadout.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// What ties one of the footer's readouts to the popover it opens, for as long as it lives: a click on the readout, or
// Space or Return, opens the popover from it, or closes it if it is open; fill2 is behind the readout while it is open
// (FooterReadout::setOpen). The popover lives in home, hidden, while closed, so it stays on the editor's tick, and goes
// back there each time it closes. Destroyed, it closes the popover. Declare it after the readout and the popover, so it
// goes before them.
class ReadoutPopover final
{
public:
    ReadoutPopover (plugkit::ui::FooterReadout& readout, plugkit::ui::FooterPopoverFrame& popover, juce::Component& home);
    ~ReadoutPopover();

    ReadoutPopover (const ReadoutPopover&) = delete;
    ReadoutPopover& operator= (const ReadoutPopover&) = delete;

private:
    plugkit::ui::FooterPopoverFrame& popover;
};

} // namespace comp1
