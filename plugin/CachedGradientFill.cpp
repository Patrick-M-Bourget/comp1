#include "CachedGradientFill.h"

#include "plugkit/ui/staple/CachedLayer.h"

#include <cmath>

namespace comp1
{

namespace
{
// The tile's width in device pixels: every column of a vertical gradient is the same, so the tile is drawn side by side
// across the path; wide enough that its draws are few.
constexpr int tileWidth = 256;
} // namespace

void CachedGradientFill::fill (juce::Graphics& g, const juce::Path& path, juce::Colour top, juce::Colour bottom, float height, juce::Point<float> origin)
{
    const auto gradient = juce::ColourGradient::vertical (top, 0.0f, bottom, height);
    const auto scale = staple::imageScaleFor (g, origin);
    const int rows = scale ? static_cast<int> (std::ceil (height * *scale)) : 0;
    if (! scale || rows <= 0)
    {
        g.setGradientFill (gradient);
        g.fillPath (path);
        return;
    }
    // The tile is drawn by g's renderer: JUCE's software renderer and the platform's work a gradient's colours out
    // differently.
    const bool software = dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr;
    if (top != tileTop || bottom != tileBottom || ! juce::exactlyEqual (height, tileHeight) || ! juce::exactlyEqual (*scale, tileScale)
        || software != tileSoftware || ! tile.isValid())
    {
        tileTop = top;
        tileBottom = bottom;
        tileHeight = height;
        tileScale = *scale;
        tileSoftware = software;
        tile = software ? juce::Image (juce::Image::ARGB, tileWidth, rows, true, juce::SoftwareImageType())
                        : juce::Image (juce::Image::ARGB, tileWidth, rows, true, juce::NativeImageType());
        juce::Graphics into (tile);
        into.addTransform (juce::AffineTransform::scale (*scale));
        into.setGradientFill (gradient);
        into.fillRect (juce::Rectangle<float> (static_cast<float> (tileWidth) / *scale, static_cast<float> (rows) / *scale));
    }
    // The tile drawn back pixel for pixel side by side across the path, inside it.
    const auto device = staple::devicePixelsOf (path.getBounds(), *scale);
    const juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (path);
    for (int x = device.getX(); x < device.getRight(); x += tileWidth)
        staple::drawBackAtDevicePixels (g, tile, { x, 0, tileWidth, rows }, *scale);
}

} // namespace comp1
