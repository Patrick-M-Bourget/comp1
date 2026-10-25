#include "ReadoutPopover.h"

namespace comp1
{

ReadoutPopover::ReadoutPopover (plugkit::ui::FooterReadout& readout, plugkit::ui::FooterPopoverFrame& p, juce::Component& home) : popover (p)
{
    readout.onClick = [&readout, &p] {
        if (p.isOpen())
            p.close();
        else
        {
            p.openFrom (readout);
            readout.setOpen (p.isOpen());
        }
    };
    p.onClose = [&readout, &p, &home] {
        readout.setOpen (false);
        home.addChildComponent (p);
    };
    home.addChildComponent (p);
}

ReadoutPopover::~ReadoutPopover()
{
    popover.onClose = nullptr;
    popover.close();
}

} // namespace comp1
