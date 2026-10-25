#include "StereoReadout.h"

#include "Parameters.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace
{
constexpr int edge = staple::tokens::popover::edge;
} // namespace

StereoPopover::StereoPopover (PluginProcessor& processor) : stereo (processor)
{
    addAndMakeVisible (stereo);
    const int inset = 2 * (edge + padding);
    setCardSize (stereo.idealWidth() + inset, stereo.idealHeight() + inset);
    followProcessor();
}

void StereoPopover::resized() { stereo.setBounds (getCardBounds().reduced (edge + padding)); }

StereoReadout::StereoReadout (PluginProcessor& p)
    : plugkit::ui::FooterReadout ("Stereo", "ST", "Stereo: Stereo Placement and Stereo Link. Click for the stereo controls"),
      plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p), stereoPopover (p)
{
    onClick = [this] {
        if (stereoPopover.isOpen())
            stereoPopover.close();
        else
        {
            stereoPopover.openFrom (*this);
            setOpen (stereoPopover.isOpen());
        }
    };
    // Back in the footer once closed, hidden, so it stays on the editor's tick.
    stereoPopover.onClose = [this] {
        setOpen (false);
        if (auto* footer = getParentComponent())
            footer->addChildComponent (stereoPopover);
    };
    show();
}

StereoReadout::~StereoReadout()
{
    stereoPopover.onClose = nullptr;
    stereoPopover.close();
}

int StereoReadout::widestIdealWidth()
{
    const auto shown = getButtonText();
    setButtonText ("Stereo 100%");
    const int widest = getIdealWidth();
    setButtonText (shown);
    return widest;
}

void StereoReadout::show()
{
    auto& state = processor.parameterState();
    auto& placement = parameters::parameterOf (state, parameters::stereoPlacementId);
    const int choice = juce::roundToInt (placement.convertFrom0to1 (placement.getValue()));
    const double link = state.getRawParameterValue (parameters::stereoLinkId)->load (std::memory_order_relaxed);
    if (const auto text = placement.getAllValueStrings()[choice] + " " + juce::String (juce::roundToInt (link)) + "%"; text != getButtonText())
        setButtonText (text);
    const bool mono = ! (processor.isStereoPlacementAvailable() && processor.isStereoLinkAvailable());
    if (mono != isNotApplied())
    {
        setNotApplied (mono);
        setDescription (mono ? "Mono track: Stereo Placement and Stereo Link change nothing" : juce::String());
    }
}

} // namespace comp1
