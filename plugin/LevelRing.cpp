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

LevelRing::Placement LevelRing::aroundThreshold (staple::Knob& knob, const juce::RangedAudioParameter& scale, std::function<double()> thresholdDb)
{
    return [&knob, &scale, thresholdDb = std::move (thresholdDb)] (double db) {
        const double bottom = bottomOf (scale), top = topOf (scale);
        const double threshold = juce::jlimit (bottom, top, thresholdDb());
        const double pointer = knob.valueToProportionOfLength (knob.getValue());
        db = juce::jlimit (bottom, top, db);
        if (db <= threshold)
            return threshold > bottom ? pointer * (db - bottom) / (threshold - bottom) : pointer;
        return pointer + (1.0 - pointer) * (db - threshold) / (top - threshold);
    };
}

LevelRing::LevelRing (staple::Knob& k, const juce::RangedAudioParameter& s, LevelHistory& h, Reading r, Placement p)
    : plugkit::ui::EditorTick::Client (k), knob (k), scale (s), reading (r), placement (std::move (p)), floor (r.fromTop ? 0.0 : bottomOf (s)),
      // A level fills up to the range's top; a Gain Reduction down to its bottom.
      level (h, r.level, floor, r.fromTop ? topOf (s) - bottomOf (s) : topOf (s)), shown (floor)
{
    shownTo = filledTo();
}

double LevelRing::filledFrom() const
{
    if (! reading.fromTop)
        return 0.0;
    return scale.convertTo0to1 (static_cast<float> (topOf (scale) - shown));
}

double LevelRing::filledTo() const
{
    if (reading.fromTop)
        return 1.0;
    return placement != nullptr ? placement (shown) : scale.convertTo0to1 (static_cast<float> (shown));
}

void LevelRing::tick (double seconds)
{
    const double now = level.next (seconds);
    const bool moved = ! juce::exactlyEqual (now, shown);
    shown = now;
    // A Placement moves the fill under a steady level as the knob or its Threshold moves; the knob repaints for its
    // own value, but not for a Threshold set elsewhere.
    const double to = filledTo();
    if (! moved && (shown <= floor || juce::exactlyEqual (to, shownTo)))
        return;
    shownTo = to;
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
    const double to = filledTo();
    juce::Path filled;
    filled.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleAt (from), angleAt (to), true);
    g.setColour (reading.colour.withMultipliedAlpha (alpha));
    g.strokePath (filled, stroke);
}

} // namespace comp1
