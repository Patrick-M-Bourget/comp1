#pragma once

#include "CompFollowAttachment.h"
#include "Controls.h"
#include "LevelHistory.h"
#include "LevelRing.h"
#include "PluginProcessor.h"
#include "plugkit/ui/OutputPopoverFrame.h"
#include "plugkit/ui/PopoverSlider.h"

#include <memory>
#include <optional>

namespace comp1
{

// The Expanded View's sections after the Style's, in signal order. Each knob's diameter is one of the kit's three:
// Comp and Threshold large, the controls used most medium, the rest small.
namespace knobSize
{
inline constexpr float large = staple::tokens::knob::gain, medium = staple::tokens::knob::frequency, small = staple::tokens::knob::small;
}

// Comp's knob, large and labelled "Comp", added to section where place puts it and attached through attachment, a
// CompFollowAttachment (ADR 0004): grabbing it while Comp Follow is off follows (followComp), with the edit that
// follows as one undo step. What the Compact View's Comp and the Compressor section's are.
ControlSection::KnobControl& addCompKnob (ControlSection& section,
                                          PluginProcessor& processor,
                                          std::optional<CompFollowAttachment>& attachment,
                                          ControlSection::Place place = ControlSection::Place::ownColumn);

// Gate: on/off, Gate Threshold, Gate Range, Gate Attack, Gate Release and Gate Hold.
struct GateSection final : ControlSection
{
    explicit GateSection (PluginProcessor& processor);
};

// Compressor: Comp Follow over Comp, Threshold (with the Detection Level ring), Ratio, Knee, Attack, Release, Auto
// Release over Hold, and Range. Comp, Comp Follow, Threshold and Ratio follow ADR 0004:
// - Comp, Threshold and Ratio are attached through CompFollowAttachment: grabbing Threshold or Ratio while Comp
//   Follow is on stops following (stopFollowingComp), grabbing Comp while it is off follows (followComp), each with
//   the edit that follows as one undo step; while following, Threshold and Ratio show the followed values.
// - The Comp Follow toggle turns it on through followComp() and off through stopFollowingComp(), never by writing it
//   alone, and shows the parameter, followed on the tick.
// Auto Gain, which Comp Follow holds on, is in the output popover's OutputSection.
class CompressorSection final : public ControlSection
{
public:
    CompressorSection (PluginProcessor& processor, LevelHistory& history);
    ~CompressorSection() override;

    LevelRing& detectionLevelRing() { return *ring; }

private:
    void follow() override;
    bool following() const;

    PluginProcessor& processor;
    KnobControl *comp = nullptr, *threshold = nullptr, *ratio = nullptr;
    juce::ToggleButton* compFollow = nullptr;
    std::unique_ptr<LevelRing> ring;
    std::optional<CompFollowAttachment> compAttachment, thresholdAttachment, ratioAttachment;
};

// Character and Saturation: Colour; Saturation on/off and Saturation Position over Drive.
struct CharacterSection final : ControlSection
{
    explicit CharacterSection (PluginProcessor& processor);
};

// Output, the output popover's (OutputPopover), in both views: Output Gain and Ceiling side by side, Ceiling with its
// ring (LevelRing::limiterGainReduction); under them Dry, then Wet, as level sliders the section's width
// (plugkit::ui::PopoverSlider, filled from the left), each with its name and its level, "Off" at the bottom, beneath
// it; under them one row of letter toggles (plugkit::ui::OutputToggle), A for Auto Gain and L for the Limiter, the
// width of the knobs' row between them. Tab walks them in that order. Ceiling stays enabled while the Limiter is off,
// as it still applies at delivery. Auto Gain follows ADR 0004: while Comp Follow is on it shows on and is disabled, its
// tooltip saying Comp Follow holds it on; otherwise it is attached as usual.
class OutputSection final : public ControlSection
{
public:
    OutputSection (PluginProcessor& processor, LevelHistory& history);
    ~OutputSection() override;

    LevelRing& ceilingRing() { return *ring; }

    int idealHeight() const override;
    void resized() override;

    // The letter toggles' row: its gap under the sliders, its height, and the gap between A and L.
    static constexpr int letterRowGap = 12, letterHeight = 30, letterGap = 4;
    // The sliders' gap under the knobs' labels, and between Dry and Wet.
    static constexpr int sliderTopGap = 12, sliderGap = 8;

private:
    void follow() override;
    bool isControlEnabled (juce::Component& control) const override;
    bool following() const;

    PluginProcessor& processor;
    KnobControl* ceiling = nullptr;
    plugkit::ui::PopoverSlider dry, wet;
    plugkit::ui::OutputToggle autoGain, limiter;
    std::unique_ptr<LevelRing> ring;
    std::unique_ptr<juce::SliderParameterAttachment> dryAttachment, wetAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> autoGainAttachment, limiterAttachment;
};

// Detection: Detection Source and the High-pass's on/off over its Frequency, then its Slope; the Low-pass's on/off over
// its Frequency, then its Slope; the Free band's on/off and Shape over its Frequency, then its Gain and Q.
struct DetectionSection final : ControlSection
{
    explicit DetectionSection (PluginProcessor& processor);
};

// Stereo: Stereo Placement and Stereo Link, available only on a stereo track: on a mono one both are disabled, their
// values kept.
struct StereoSection final : ControlSection
{
    explicit StereoSection (PluginProcessor& processor);
};

// Lookahead, available only in High Quality: in Zero Latency it is disabled, its value kept.
struct LookaheadSection final : ControlSection
{
    explicit LookaheadSection (PluginProcessor& processor);
};

} // namespace comp1
