#pragma once

#include "ChipWidth.h"
#include "ReadoutPopover.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/FooterReadout.h"

#include <functional>
#include <memory>
#include <utility>

namespace comp1
{

// What one of the footer's readouts reads: the readout (plugkit::ui::FooterReadout), what it shows of the processor,
// and the texts it can read at its widest.
struct FooterReading
{
    std::unique_ptr<plugkit::ui::FooterReadout> readout;
    // Shows the processor's state on the readout: its text, whether it is applied, its description.
    std::function<void (plugkit::ui::FooterReadout&)> show;
    // The texts the footer gives the readout room for, so it keeps its width as what it reads changes.
    std::function<juce::StringArray (const plugkit::ui::FooterReadout&)> widestTexts;
};

// One of the footer's readouts and the popover it opens, as one footer item in both views: the Output, Saturation,
// Stereo and Processing Mode readouts. The readout fills the item and shows the processor's state as it is built and
// on each of the editor's ticks. A click on it, or Space or Return, opens and closes the popover, fill2 behind the
// readout while it is open (ReadoutPopover).
template <typename Popover>
class FooterItem final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    // Builds its popover from popoverArgs.
    template <typename... Args>
    explicit FooterItem (FooterReading r, Args&&... popoverArgs)
        : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), reading (std::move (r)),
          popoverComponent (std::forward<Args> (popoverArgs)...), tie (*reading.readout, popoverComponent, *this)
    {
        addAndMakeVisible (*reading.readout);
        show();
    }

    plugkit::ui::FooterReadout& readout() { return *reading.readout; }
    Popover& popover() { return popoverComponent; }

    // The readout's width for its widest texts.
    int idealWidth() { return widestIdealWidth (*reading.readout, reading.widestTexts (*reading.readout)); }
    // The readout's width for its label alone: the least the footer gives it.
    int shrunkWidth() const { return reading.readout->getShrunkWidth(); }

    void resized() override { reading.readout->setBounds (getLocalBounds()); }

    // Called each time the readout's text changes, once the item is built: so the footer can lay out again for a
    // readout whose width follows what it reads.
    std::function<void()> onTextChanged;

private:
    void tick (double) override { show(); }
    void show()
    {
        const auto before = reading.readout->getButtonText();
        reading.show (*reading.readout);
        if (onTextChanged != nullptr && reading.readout->getButtonText() != before)
            onTextChanged();
    }

    FooterReading reading;
    Popover popoverComponent;
    ReadoutPopover tie;
};

} // namespace comp1
