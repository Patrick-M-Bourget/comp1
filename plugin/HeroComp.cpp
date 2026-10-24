#include "HeroComp.h"

#include "Parameters.h"

namespace comp1
{

const juce::String HeroComp::offTooltip = "Threshold and Ratio set directly. Grab Comp to follow";

HeroComp::HeroComp (ControlSection::KnobControl& c, PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (c.knob), comp (c), processor (p),
      ring (c.knob,
            parameters::parameterOf (p.parameterState(), parameters::thresholdId),
            history,
            LevelRing::detectionLevel(),
            LevelRing::aroundThreshold (c.knob, parameters::parameterOf (p.parameterState(), parameters::thresholdId), [&p] {
                return p.playedCurve().thresholdDb;
            })),
      arcColour (c.knob.getArcColour())
{
    comp.setRing (ring);
    follow();
}

HeroComp::~HeroComp()
{
    // Before the ring the knob draws goes.
    comp.knob.setRing (nullptr);
}

void HeroComp::tick (double) { follow(); }

void HeroComp::follow()
{
    const bool following = parameters::isOn (*processor.parameterState().getRawParameterValue (parameters::compFollowId));
    const auto colour = following ? arcColour : arcColour.withMultipliedAlpha (offArcAlpha);
    if (colour != comp.knob.getArcColour())
        comp.knob.setArcColour (colour);
    const auto tooltip = following ? juce::String() : offTooltip;
    if (tooltip != comp.knob.getTooltip())
        comp.knob.setTooltip (tooltip);
}

} // namespace comp1
