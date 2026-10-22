#pragma once

#include "plugkit/ui/EditHistory.h"
#include "plugkit/ui/EditorTick.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <optional>

namespace comp1
{

// Attaches the Comp, Threshold or Ratio knob to its host parameter, as SliderParameterAttachment would, with Comp
// Follow's two differences (ADR 0004):
//
// - A grab (the press of a drag, an arrow key's first step, a typed value, a screen reader's set) first calls grab,
//   inside one transaction of the edit history that the edit itself then joins: stopFollowingComp() for Threshold
//   and Ratio, followComp() for Comp. So switching Comp Follow and the edit that follows are one undo step, and the
//   edit carries on from the value the knob shows.
// - What the knob shows, on the editor's tick and whenever it isn't being edited, is shownInstead's value while it has
//   one (Threshold's and Ratio's followed values while Comp Follow is on), else the parameter's: so it moves with Comp
//   and Comp's Host Automation, and ignores Host Automation of the stored value it doesn't play.
class CompFollowAttachment final : private juce::Slider::Listener, private plugkit::ui::EditorTick::Client
{
public:
    CompFollowAttachment (juce::Slider& knob,
                          juce::RangedAudioParameter& parameter,
                          plugkit::ui::EditHistory& history,
                          std::function<void()> grab,
                          std::function<std::optional<double>()> shownInstead = {});
    ~CompFollowAttachment() override;

    // Shows the value the knob follows (above), unless it is being edited.
    void show();

private:
    void tick (double) override { show(); }
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;
    void write();

    juce::Slider& knob;
    juce::RangedAudioParameter& parameter;
    plugkit::ui::EditHistory& history;
    std::function<void()> grab;
    std::function<std::optional<double>()> shownInstead;
    bool editing = false, showing = false;
};

} // namespace comp1
