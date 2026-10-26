#pragma once

#include "plugkit/ui/FooterPopoverFrame.h"
#include "plugkit/ui/staple/Tokens.h"

#include <utility>

namespace comp1
{

// A popover one of the footer's readouts opens, in both views, holding one control section (plugkit::ui::
// FooterPopoverFrame): above the readout, right-aligned to it, closed by a second click, Escape or a click outside. Its
// card holds the section, padded, at the section's ideal size. The section's controls are attached to their host
// parameters, so they follow Host Automation, undo, A/B Compare and Presets, and each edit is one undo step. Opened
// from the keys, focus moves to the section's first control, and Tab walks them in the section's order; closed, focus
// inside it goes back to the readout. While open it follows the processor on the editor's tick.
template <typename Section>
class SectionPopover final : public plugkit::ui::FooterPopoverFrame
{
public:
    // Builds its section from args.
    template <typename... Args>
    explicit SectionPopover (Args&&... args) : contents (std::forward<Args> (args)...)
    {
        addAndMakeVisible (contents);
        const int inset = 2 * (staple::tokens::popover::edge + padding);
        setCardSize (contents.idealWidth() + inset, contents.idealHeight() + inset);
        followProcessor();
    }

    Section& section() { return contents; }

    void resized() override { contents.setBounds (getCardBounds().reduced (staple::tokens::popover::edge + padding)); }

    static constexpr int padding = 12;

private:
    void followProcessor() override { contents.followProcessor(); }

    Section contents;
};

} // namespace comp1
