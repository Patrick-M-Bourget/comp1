#include "MeterLabel.h"

#include "plugkit/ui/staple/Fonts.h"
#include "plugkit/ui/staple/Tokens.h"

namespace comp1
{

namespace
{
// The kit's label style, letter-spaced as its uppercase text is.
juce::Font meterLabelFont() { return staple::labelFont().withExtraKerningFactor (staple::tokens::size::capsTracking); }
} // namespace

MeterLabel::MeterLabel (const juce::String& text)
{
    setName (text.toUpperCase());
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void MeterLabel::paint (juce::Graphics& g)
{
    g.setFont (meterLabelFont());
    g.setColour (staple::tokens::colour::text3);
    g.drawText (getName(), getLocalBounds(), juce::Justification::centred, false);
}

int MeterLabel::textWidth() const { return staple::textWidth (meterLabelFont(), getName()); }

} // namespace comp1
