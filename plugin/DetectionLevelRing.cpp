#include "DetectionLevelRing.h"

#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace
{
namespace tokens = staple::tokens;

// The knob's sweep, as angles clockwise from 12 o'clock (staple::Knob's).
float angleAt (double proportion)
{
    return juce::degreesToRadians (tokens::knob::sweepDegrees * (static_cast<float> (proportion) - 0.5f));
}
} // namespace

DetectionLevelRing::DetectionLevelRing (staple::Knob& k, const juce::RangedAudioParameter& t, LevelHistory& h)
    : plugkit::ui::EditorTick::Client (k), knob (k), threshold (t), history (h), bottom (t.getNormalisableRange().start), loudest (bottom),
      shown (bottom)
{
    history.subscribe (*this);
}

DetectionLevelRing::~DetectionLevelRing()
{
    history.unsubscribe (*this);
}

void DetectionLevelRing::levelsRead (std::span<const LevelRecord> records)
{
    for (const auto& record : records)
        loudest = std::max (loudest, record.detectionLevelDb);
}

void DetectionLevelRing::tick (double seconds)
{
    const double top = threshold.getNormalisableRange().end;
    const double level = std::clamp (ballistics.update (std::min (loudest, top), seconds), bottom, top);
    loudest = bottom;
    if (juce::exactlyEqual (level, shown))
        return;
    shown = level;
    knob.ringChanged();
}

void DetectionLevelRing::paintRing (juce::Graphics& g, staple::Knob&, juce::Point<float> centre, float inner, float outer)
{
    const float radius = (inner + outer) / 2.0f, width = outer - inner;
    const float start = angleAt (0.0), end = angleAt (1.0);
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
    const float alpha = knob.isEnabled() ? 1.0f : tokens::motion::disabledAlpha;

    juce::Path lane;
    lane.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (tokens::colour::knobRingLane.withMultipliedAlpha (alpha));
    g.strokePath (lane, stroke);

    if (shown <= bottom)
        return;
    const double proportion = threshold.convertTo0to1 (static_cast<float> (shown));
    juce::Path level;
    level.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angleAt (proportion), true);
    g.setColour (tokens::colour::anSc.withMultipliedAlpha (alpha));
    g.strokePath (level, stroke);
}

} // namespace comp1
