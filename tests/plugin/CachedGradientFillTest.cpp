// CachedGradientFill: a path filled from a gradient drawn once, within an 8-bit step of what a gradient fill draws.

#include "CachedGradientFill.h"
#include "plugkit/test/EditorHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace
{

constexpr int width = 120, height = 80;
// Steep gradients, a few 8-bit steps a row, so a tile a row out of place shows: a translucent one, blending over what
// is under it, and an opaque one.
const juce::Colour translucentTop { 0xF0FF8020 }, translucentBottom { 0x102080FF };
const juce::Colour opaqueTop { 0xFFFFFFFF }, opaqueBottom { 0xFF000000 };

// A jagged trace across the width with the area under it closed along the bottom, as a level graph fills.
juce::Path jaggedArea()
{
    juce::Path area;
    for (int x = 0; x <= width; ++x)
    {
        const float y = 10.0f + 50.0f * static_cast<float> ((x * 37) % 23) / 23.0f + 0.3f * static_cast<float> (x % 3);
        if (x == 0)
            area.startNewSubPath (0.5f, y);
        else
            area.lineTo (static_cast<float> (x) - 0.5f, y);
    }
    area.lineTo (width - 0.5f, height);
    area.lineTo (0.5f, height);
    area.closeSubPath();
    return area;
}

// A rectangle on whole pixels at 1x and 2x: no edge drawn part-covered.
juce::Path wholePixelArea()
{
    juce::Path area;
    area.addRectangle (10.0f, 5.0f, 100.0f, 70.0f);
    return area;
}

// A dark backdrop and a grid line under the fill, so its blending shows.
void backdrop (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF15171C));
    g.setColour (juce::Colour (0xFF2A2E36));
    g.drawHorizontalLine (40, 0.0f, static_cast<float> (width));
}

// The image paint draws at scale, on the backdrop.
template <typename Paint>
juce::Image drawn (float scale, const juce::ImageType& type, Paint&& paint)
{
    return harness::paintImage (static_cast<int> (std::ceil (scale * width)), static_cast<int> (std::ceil (scale * height)), [&] (juce::Graphics& g) {
        g.addTransform (juce::AffineTransform::scale (scale));
        backdrop (g);
        paint (g);
    }, type);
}

// What a gradient fill of path from top at 0 to bottom at height draws.
juce::Image gradientFilled (float scale, const juce::ImageType& type, const juce::Path& path, juce::Colour top, juce::Colour bottom)
{
    return drawn (scale, type, [&] (juce::Graphics& g) {
        g.setGradientFill (juce::ColourGradient::vertical (top, 0.0f, bottom, static_cast<float> (height)));
        g.fillPath (path);
    });
}

juce::Image cachedFilled (comp1::CachedGradientFill& fill, float scale, const juce::ImageType& type, const juce::Path& path, juce::Colour top,
                          juce::Colour bottom, juce::Point<float> origin = {})
{
    return drawn (scale, type, [&] (juce::Graphics& g) { fill.fill (g, path, top, bottom, static_cast<float> (height), origin); });
}

struct Renderer
{
    const char* name;
    const juce::ImageType& type;
};

// How a cached fill's drawing differs from a gradient fill's, in 8-bit steps: the most any channel of any pixel differs,
// and the most any channel differs on average along a row, where a tile drawn a row out of place shows.
struct Compared
{
    int largest = 0;
    double largestRowMean = 0.0;
};

Compared compared (comp1::CachedGradientFill& fill, float scale, const juce::ImageType& type, const juce::Path& path, juce::Colour top, juce::Colour bottom,
                   juce::Point<float> origin = {})
{
    const auto cached = cachedFilled (fill, scale, type, path, top, bottom, origin), direct = gradientFilled (scale, type, path, top, bottom);
    REQUIRE (cached.getBounds() == direct.getBounds());
    Compared result;
    for (int y = 0; y < cached.getHeight(); ++y)
    {
        int rowSums[4] {};
        for (int x = 0; x < cached.getWidth(); ++x)
        {
            const auto p = cached.getPixelAt (x, y), q = direct.getPixelAt (x, y);
            const int differences[] { p.getAlpha() - q.getAlpha(), p.getRed() - q.getRed(), p.getGreen() - q.getGreen(), p.getBlue() - q.getBlue() };
            for (int c = 0; c < 4; ++c)
            {
                result.largest = std::max (result.largest, std::abs (differences[c]));
                rowSums[c] += differences[c];
            }
        }
        for (const int sum : rowSums)
            result.largestRowMean = std::max (result.largestRowMean, std::abs (static_cast<double> (sum)) / cached.getWidth());
    }
    return result;
}

void checkClose (const Compared& c)
{
    CAPTURE (c.largest, c.largestRowMean);
    CHECK (c.largest <= 1);
    CHECK (c.largestRowMean <= 0.5);
}

} // namespace

// Not to the pixel: CoreGraphics dithers a gradient by where each pixel lies, so the tile's dither isn't the window's;
// and where the path covers part of a pixel, a gradient fill scales the gradient's colour by the coverage before
// rounding it to 8 bits, the tile's already rounded colour after. An 8-bit step at most, a fraction of one along a row.
TEST_CASE ("A cached gradient fill draws a path as a gradient fill does, at 1x and 2x, on both renderers")
{
    static const juce::SoftwareImageType software;
    static const juce::NativeImageType native;
    const auto renderer = GENERATE (Renderer { "software", software }, Renderer { "native", native });
    const float scale = GENERATE (1.0f, 2.0f);
    CAPTURE (renderer.name, scale);
    comp1::CachedGradientFill fill;

    // Edges on whole pixels, then a jagged edge, part-covering them; the colours anew each time they change (as a fade
    // dims them), and drawn again from the tile they left.
    for (const auto& area : { wholePixelArea(), jaggedArea() })
        for (const auto& [top, bottom] : { std::pair { translucentTop, translucentBottom }, std::pair { opaqueTop, opaqueBottom },
                                           std::pair { translucentTop, translucentBottom }, std::pair { translucentTop, translucentBottom } })
            checkClose (compared (fill, scale, renderer.type, area, top, bottom));
}

TEST_CASE ("A cached gradient fill draws anew at a new scale")
{
    comp1::CachedGradientFill fill;
    static const juce::NativeImageType native;
    for (const float scale : { 1.0f, 2.0f, 1.0f })
    {
        CAPTURE (scale);
        checkClose (compared (fill, scale, native, wholePixelArea(), opaqueTop, opaqueBottom));
    }
}

TEST_CASE ("A cached gradient fill off a whole device pixel draws the gradient fill directly")
{
    comp1::CachedGradientFill fill;
    static const juce::NativeImageType native;
    // At 125%, a graph half a pixel in starts part-way through a device pixel: no tile, so even a jagged edge is the
    // gradient fill's own, to the pixel.
    CHECK (compared (fill, 1.25f, native, jaggedArea(), translucentTop, translucentBottom, { 0.5f, 0.5f }).largest == 0);
}
