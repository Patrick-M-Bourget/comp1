#include "HandlePainting.h"

#include "plugkit/ui/staple/Light.h"
#include "plugkit/ui/staple/Painting.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1::handles
{

namespace colour = staple::tokens::colour;

void paint (juce::Graphics& g, juce::Point<float> centre, const Look& look)
{
    const auto disc = juce::Rectangle<float> (2.0f * look.radius, 2.0f * look.radius).withCentre (centre);
    // Lit only while hovered, so an unhovered disc is curve-main exactly.
    const auto fill = look.hovered ? staple::lit (colour::curveMain, true, false) : colour::curveMain;
    g.setColour (fill.withMultipliedAlpha (look.alpha));
    g.fillEllipse (disc);
    g.setColour ((look.selected ? colour::handleSelectedRing : colour::handleRing).withMultipliedAlpha (look.alpha));
    g.drawEllipse (disc, ringWidth);
}

void paintReadout (juce::Graphics& g, juce::Point<float> centre, float radius, float areaWidth, const juce::String& title,
                   const juce::String& value)
{
    const float width = staple::readoutCardWidth (title, value);
    const auto height = static_cast<float> (staple::readoutCard::height);
    auto card = juce::Rectangle<float> (width, height).withCentre (centre.translated (0.0f, -(radius + readoutGap + height / 2.0f)));
    if (card.getY() < 0.0f)
        card.setY (centre.y + radius + readoutGap);
    card.setX (std::clamp (card.getX(), 0.0f, std::max (0.0f, areaWidth - width)));
    staple::drawReadoutCard (g, card, title, value);
}

} // namespace comp1::handles
