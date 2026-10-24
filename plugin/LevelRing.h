#pragma once

#include "LevelHistory.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/controls/Knob.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace comp1
{

// A level from the editor's level history in a knob's ring: the knob's lane, outside its face, filled on the knob's
// own parameter's scale, so the level reads against the value the knob shows. Each tick it shows the loudest read since
// the last, rising at once and falling at 20 dB/s (ShownLevel), and repaints only while it moves. It is display only:
// a press or drag on the lane does nothing, it is no stop for Tab, and its tooltip lines are empty. comp1 has three
// (plugkit's Staple design-system README.md, "Knobs"):
//
// - The Detection Level ring on Threshold (detectionLevel): filled in anSc from the bottom of Threshold's range up to
//   the Detection Level (LevelRecord::detectionLevelDb).
// - The Detection Level ring on the Compact View's Comp (HeroComp): the same reading, on Threshold's range, placed
//   on Comp's sweep so the fill reaches Comp's pointer as the Detection Level reaches the Threshold the compressor
//   plays (aroundThreshold).
// - The Ceiling ring (limiterGainReduction): filled in dynRange from the top of Ceiling's range, 0 dBFS, down by the
//   Limiter's Gain Reduction (LevelRecord::limiterGainReductionDb) on Ceiling's dB scale, as the Gain Reduction meters
//   hang from their top, no further than the range's bottom.
class LevelRing final : public staple::Knob::RingHandler, private plugkit::ui::EditorTick::Client
{
public:
    // What a ring shows.
    struct Reading
    {
        double LevelRecord::* level;
        // Where the fill starts: the bottom of the range, filling up to the level; or its top, filling down by the
        // level in dB.
        bool fromTop;
        juce::Colour colour;
    };
    static Reading detectionLevel();
    static Reading limiterGainReduction();

    // Where a level in dB lands on the knob's sweep, 0 to 1.
    using Placement = std::function<double (double db)>;
    // A level on scale's range placed around a Threshold on the knob's sweep: from the bottom of the range up to
    // thresholdDb() over the sweep up to the knob's value, and from thresholdDb() up to the range's top over the rest,
    // each in proportion to dB. So the fill passes the knob's pointer exactly while the level is over the Threshold.
    static Placement aroundThreshold (staple::Knob& knob, const juce::RangedAudioParameter& scale, std::function<double()> thresholdDb);

    // The level placed on scale's own sweep unless placement is given.
    LevelRing (staple::Knob& knob, const juce::RangedAudioParameter& scale, LevelHistory& history, Reading reading, Placement placement = {});

    // The level the lane is filled with, in dB: from the bottom of the range up for the Detection Level, from 0 up for
    // a Gain Reduction.
    double shownDb() const { return shown; }
    // Where on the knob's sweep the fill starts, 0 to 1: 0 from the bottom, under 1 from the top.
    double filledFrom() const;
    // Where on the knob's sweep the fill ends, 0 to 1.
    double filledTo() const;

    void paintRing (juce::Graphics& g, staple::Knob& knob, juce::Point<float> centre, float inner, float outer) override;

private:
    void tick (double seconds) override;
    bool isActive() const override { return shown > floor; }

    staple::Knob& knob;
    const juce::RangedAudioParameter& scale;
    const Reading reading;
    const Placement placement;
    const double floor;
    ShownLevel level;
    double shown;
    // Where the fill last ended: a Placement that moves (aroundThreshold's) repaints the ring on a steady level.
    double shownTo = 0.0;
};

} // namespace comp1
