#include "OutputPopover.h"

#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace
{
constexpr int edge = staple::tokens::popover::edge;
} // namespace

OutputPopover::OutputPopover (PluginProcessor& processor, LevelHistory& history) : output (processor, history)
{
    addAndMakeVisible (output);
    const int inset = 2 * (edge + padding);
    setCardSize (output.idealWidth() + inset, output.idealHeight() + inset);
    followProcessor();
}

void OutputPopover::resized()
{
    output.setBounds (getCardBounds().reduced (edge + padding));
}

} // namespace comp1
