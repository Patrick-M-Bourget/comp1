#pragma once

#include "Controls.h"
#include "LevelHistory.h"
#include "LevelRing.h"
#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// What the Compact View's Comp, the hero, has beyond the Expanded View's (ADR 0011), on its knob alone, so it holds
// wherever the Compact View places Comp:
//
// - The Detection Level ring: Threshold's in the Expanded View (LevelRing::detectionLevel), on Threshold's range, in
//   a lane sized to the hero (ControlSection::KnobControl::setRing). It is placed around the Threshold the compressor
//   plays (LevelRing::aroundThreshold): the fill reaches Comp's pointer as the Detection Level reaches that Threshold,
//   so it passes the pointer exactly while the compressor is over Threshold, as Threshold's ring passes Threshold's
//   pointer. It shows the detection signal as Threshold's does, under Global Bypass and Detection Audition alike.
// - The Comp Follow-off cue (ADR 0004): while Comp Follow is off, the value arc draws at the Global Bypass scale
//   (offArcAlpha), and the knob's tooltip says that Threshold and Ratio are set directly and that grabbing Comp makes
//   them follow again (offTooltip); while it is on, the arc is full and there is no tooltip. It follows Comp Follow on
//   the editor's tick, whatever changes it: grabbing Comp, the Expanded View, Host Automation, undo, a Preset or a
//   Style. It adds no element and moves nothing.
class HeroComp final : private plugkit::ui::EditorTick::Client
{
public:
    HeroComp (ControlSection::KnobControl& comp, PluginProcessor& processor, LevelHistory& history);
    ~HeroComp() override;

    LevelRing& detectionLevelRing() { return ring; }

    // The arc's alpha while Comp Follow is off: the design system's globalBypassScale.
    static constexpr float offArcAlpha = staple::tokens::motion::globalBypassScale;
    static const juce::String offTooltip;

private:
    void tick (double seconds) override;
    void follow();

    ControlSection::KnobControl& comp;
    PluginProcessor& processor;
    LevelRing ring;
    const juce::Colour arcColour;
};

} // namespace comp1
