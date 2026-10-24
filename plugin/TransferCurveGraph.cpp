#include "TransferCurveGraph.h"

#include "Controls.h"
#include "plugkit/ui/ValueText.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace comp1
{

namespace
{
namespace colour = staple::tokens::colour;
namespace size = staple::tokens::size;
namespace display = plugkit::ui::display;

// The dB labels: the output's right-aligned this far in from the right edge, the input's this far up from the
// bottom, each this big, kept inside the graph.
constexpr float labelInset = 6.0f, labelWidth = 32.0f, labelHeight = 12.0f;
constexpr float curveLineWidth = 1.5f, dotRadius = 4.0f;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal" // exactly equal: settings that changed nothing repaint nothing
#endif
bool operator== (const clean::Curve& a, const clean::Curve& b)
{
    return a.thresholdDb == b.thresholdDb && a.inverseRatio == b.inverseRatio && a.kneeDb == b.kneeDb && a.rangeDb == b.rangeDb;
}
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
} // namespace

TransferCurveGraph::TransferCurveGraph (PluginProcessor& p, LevelHistory& h)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p), drawn (p.playedCurve()),
      level (h, &LevelRecord::detectionLevelDb, -scaleDb, 0.0), bypassFade (p.parameterState())
{
    setName ("Transfer Curve");
    setWantsKeyboardFocus (false);
    setInterceptsMouseClicks (false, false);
}

std::optional<double> TransferCurveGraph::dotDb() const
{
    if (shown <= -scaleDb)
        return std::nullopt;
    return shown;
}

std::optional<juce::Point<double>> TransferCurveGraph::dot() const
{
    if (const auto inputDb = dotDb())
        return juce::Point<double> { *inputDb, outputAt (*inputDb) };
    return std::nullopt;
}

juce::Point<float> TransferCurveGraph::pointAt (double inputDb, double outputDb) const
{
    return { static_cast<float> ((inputDb + scaleDb) / scaleDb) * static_cast<float> (getWidth()),
             static_cast<float> (-outputDb / scaleDb) * static_cast<float> (getHeight()) };
}

void TransferCurveGraph::tick (double seconds)
{
    const double levelDb = level.next (seconds);
    const auto now = processor.playedCurve();
    const bool faded = bypassFade.step (seconds);
    if (juce::exactlyEqual (levelDb, shown) && now == drawn && ! faded)
        return;
    shown = levelDb;
    drawn = now;
    repaint();
}

juce::String TransferCurveGraph::settingsText() const
{
    auto& parameters = processor.parameterState();
    const auto spoken = [&] (const juce::String& id, double plain) {
        return controls::spokenValue (*parameters.getParameter (id), plain);
    };
    return "Threshold " + spoken (parameters::thresholdId, drawn.thresholdDb) + ", Ratio "
           + spoken (parameters::ratioId, drawn.inverseRatio <= 0.0 ? std::numeric_limits<double>::infinity() : 1.0 / drawn.inverseRatio)
           + ", Knee " + juce::String (drawn.kneeDb, 1) + " dB";
}

void TransferCurveGraph::paint (juce::Graphics& g)
{
    const int width = getWidth(), height = getHeight();
    if (width <= 0 || height <= 0)
        return;
    const auto w = static_cast<float> (width), h = static_cast<float> (height);
    const auto xOf = [this] (double db) { return pointAt (db, 0.0).x; };
    const auto yOf = [this] (double db) { return pointAt (-scaleDb, db).y; };
    // The grid: a line every gridStepDb on both axes; the 0 dB output line, at the top, the zero line, and the 0 dB input
    // line, at the right edge, drawn as it is.
    display::GridLines lines;
    for (double db = -gridStepDb; db > -scaleDb; db -= gridStepDb)
    {
        lines.majorX.push_back (xOf (db));
        lines.gainY.push_back (yOf (db));
    }
    lines.zeroY = yOf (0.0);
    display::paintGrid (g, lines, { width, height });
    g.setColour (colour::gridZero);
    g.drawVerticalLine (juce::roundToInt (xOf (0.0)) - 1, 0.0f, h);

    // The curve, a point every pixel column.
    juce::Path curvePath;
    for (int column = 0; column <= width; ++column)
    {
        const double input = -scaleDb + scaleDb * column / width;
        const juce::Point<float> at { static_cast<float> (column), yOf (outputAt (input)) };
        if (column == 0)
            curvePath.startNewSubPath (at);
        else
            curvePath.lineTo (at);
    }
    const float alpha = curveAlpha();
    g.setColour (colour::curveMain.withMultipliedAlpha (alpha));
    g.strokePath (curvePath, juce::PathStrokeType (curveLineWidth));

    if (const auto at = dot())
    {
        g.setColour (colour::anSc.withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (2.0f * dotRadius, 2.0f * dotRadius).withCentre ({ xOf (at->x), yOf (at->y) }));
    }

    // The dB labels: the output's at the right, the input's along the bottom.
    std::vector<display::Label> scale;
    for (double db = 0.0; db > -scaleDb; db -= gridStepDb)
    {
        const auto text = plugkit::ui::valueText::signedDb (db, { .decimals = 0, .unit = false });
        const float y = std::clamp (yOf (db), labelHeight / 2.0f, h - labelHeight / 2.0f);
        scale.push_back ({ text, { w - labelInset - labelWidth, y - labelHeight / 2.0f, labelWidth, labelHeight }, juce::Justification::centredRight,
                           colour::text3, size::fs1 });
        if (db < 0.0)
        {
            const float x = std::clamp (xOf (db), labelWidth / 2.0f, w - labelWidth / 2.0f);
            scale.push_back ({ text, { x - labelWidth / 2.0f, h - labelInset - labelHeight, labelWidth, labelHeight }, juce::Justification::centred,
                               colour::text3, size::fs1 });
        }
    }
    labels.paint (g, DisplayFades::originInWindow (*this), scale);
}

} // namespace comp1
