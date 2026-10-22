#include "LevelRing.h"

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

double bottomOf (const juce::RangedAudioParameter& scale) { return scale.getNormalisableRange().start; }
double topOf (const juce::RangedAudioParameter& scale) { return scale.getNormalisableRange().end; }
} // namespace

LevelRing::Reading LevelRing::detectionLevel() { return { &LevelRecord::detectionLevelDb, false, tokens::colour::anSc }; }

LevelRing::Reading LevelRing::limiterGainReduction() { return { &LevelRecord::limiterGainReductionDb, true, tokens::colour::dynRange }; }

LevelRing::LevelRing (staple::Knob& k, const juce::RangedAudioParameter& s, LevelHistory& h, Reading r)
    : plugkit::ui::EditorTick::Client (k), knob (k), scale (s), reading (r), floor (r.fromTop ? 0.0 : bottomOf (s)),
      // A level fills up to the range's top; a Gain Reduction down to its bottom.
      level (h, r.level, floor, r.fromTop ? topOf (s) - bottomOf (s) : topOf (s)), shown (floor)
{
}

double LevelRing::filledFrom() const
{
    if (! reading.fromTop)
        return 0.0;
    return scale.convertTo0to1 (static_cast<float> (topOf (scale) - shown));
}

void LevelRing::tick (double seconds)
{
    const double now = level.next (seconds);
    if (juce::exactlyEqual (now, shown))
        return;
    shown = now;
    knob.ringChanged();
}

void LevelRing::paintRing (juce::Graphics& g, staple::Knob&, juce::Point<float> centre, float inner, float outer)
{
    const float radius = (inner + outer) / 2.0f, width = outer - inner;
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
    const float alpha = knob.isEnabled() ? 1.0f : tokens::motion::disabledAlpha;

    juce::Path lane;
    lane.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleAt (0.0), angleAt (1.0), true);
    g.setColour (tokens::colour::knobRingLane.withMultipliedAlpha (alpha));
    g.strokePath (lane, stroke);

    if (shown <= floor)
        return;
    const double from = filledFrom();
    const double to = reading.fromTop ? 1.0 : scale.convertTo0to1 (static_cast<float> (shown));
    juce::Path filled;
    filled.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleAt (from), angleAt (to), true);
    g.setColour (reading.colour.withMultipliedAlpha (alpha));
    g.strokePath (filled, stroke);
}

} // namespace comp1
