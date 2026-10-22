#include "Section.h"

#include "plugkit/ui/staple/Fonts.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;

// The kit's section title (its design-system/tokens.json, "section"): fs3 at 700, letter-spaced 0.08 em.
juce::Font titleFont() { return staple::font (size::fs3, staple::Weight::bold).withExtraKerningFactor (0.08f); }
} // namespace

Section::Section (const juce::String& title)
{
    setName (title);
}

void Section::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setFont (titleFont());
    g.setColour (colour::text2);
    g.drawText (getName().toUpperCase(), bounds.withHeight (titleHeight), juce::Justification::centredLeft, true);
    // Halfway down the gap under the title.
    g.setColour (colour::line1);
    g.fillRect (bounds.withY (titleHeight + dividerGap / 2).withHeight (1));
}

juce::Rectangle<int> Section::controlArea() const { return getLocalBounds().withTrimmedTop (titleHeight + dividerGap); }

int tallestOf (std::span<Section* const> row)
{
    int height = 0;
    for (const auto* section : row)
        height = std::max (height, section->idealHeight());
    return height;
}

int Section::titleWidth() const
{
    return static_cast<int> (std::ceil (juce::GlyphArrangement::getStringWidth (titleFont(), getName().toUpperCase())));
}

} // namespace comp1
