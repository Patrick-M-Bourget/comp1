#pragma once

#include "LevelHistory.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/controls/Knob.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace comp1
{

// The Detection Level ring on the Threshold knob: the knob's lane, outside its face, filled in anSc from the bottom of
// Threshold's range up to the Detection Level (LevelRecord::detectionLevelDb), on Threshold's own scale, so the level
// reads against the Threshold the knob shows. Each tick it shows the loudest Detection Level read since the last,
// rising at once and falling at 20 dB/s (LevelBallistics), and repaints only while it moves. It is display only: a
// press or drag on the lane does nothing, it is no stop for Tab, and its tooltip lines are empty.
class DetectionLevelRing final : public staple::Knob::RingHandler, private plugkit::ui::EditorTick::Client
{
public:
    DetectionLevelRing (staple::Knob& thresholdKnob, const juce::RangedAudioParameter& threshold, LevelHistory& history);

    // The Detection Level the lane is filled to, in dB: Threshold's bottom and up.
    double shownDb() const { return shown; }

    void paintRing (juce::Graphics& g, staple::Knob& knob, juce::Point<float> centre, float inner, float outer) override;

private:
    void tick (double seconds) override;
    bool isActive() const override { return shown > bottom; }

    staple::Knob& knob;
    const juce::RangedAudioParameter& threshold;
    const double bottom;
    ShownLevel level; // the Detection Level, on Threshold's range
    double shown;
};

} // namespace comp1
