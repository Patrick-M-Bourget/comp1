#pragma once

#include <juce_graphics/juce_graphics.h>

namespace comp1
{

// A path filled with a vertical gradient, from top at y = 0 to bottom at y = height, drawn as
// g.setGradientFill (juce::ColourGradient::vertical (top, 0, bottom, height)) and g.fillPath (path) draw it, for less:
// the gradient is drawn once, by g's renderer, into a tile a row per device pixel, and drawn back pixel for pixel
// inside the path until the colours, the height, the device scale or the renderer change. A gradient fill has
// CoreGraphics work the gradient's colour out afresh for every pixel the path covers, most of a display-sized area's
// cost. Every pixel is within an 8-bit step of the gradient fill's: CoreGraphics dithers a gradient by where each pixel
// lies, so the tile's dither isn't the window's, and where the path covers part of a pixel a gradient fill rounds the
// colour after scaling it by the coverage, the tile before. Where the drawing doesn't start on a whole device pixel (at
// 75% or 125% on a 1x screen) the tile's rows wouldn't fall on the device's, so it fills with the gradient directly.
class CachedGradientFill
{
public:
    // origin is where g's logical (0, 0) lies from a whole device pixel, in logical pixels: the component's place in
    // the window (DisplayFades::originInWindow).
    void fill (juce::Graphics& g, const juce::Path& path, juce::Colour top, juce::Colour bottom, float height, juce::Point<float> origin);

private:
    juce::Colour tileTop, tileBottom;
    float tileHeight = 0.0f, tileScale = 0.0f;
    bool tileSoftware = false;
    juce::Image tile;
};

} // namespace comp1
