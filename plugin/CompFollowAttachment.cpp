#include "CompFollowAttachment.h"

#include <utility>

namespace comp1
{

CompFollowAttachment::CompFollowAttachment (juce::Slider& k,
                                            juce::RangedAudioParameter& p,
                                            plugkit::ui::EditHistory& h,
                                            std::function<void()> grabbed,
                                            std::function<std::optional<double>()> instead)
    : plugkit::ui::EditorTick::Client (k), knob (k), parameter (p), history (h), grab (std::move (grabbed)), shownInstead (std::move (instead))
{
    knob.addListener (this);
    show();
}

CompFollowAttachment::~CompFollowAttachment()
{
    if (editing)
        sliderDragEnded (&knob);
    knob.removeListener (this);
}

void CompFollowAttachment::show()
{
    if (editing)
        return;
    std::optional<double> instead;
    if (shownInstead != nullptr)
        instead = shownInstead();
    const double value = instead.has_value() ? *instead : static_cast<double> (parameter.convertFrom0to1 (parameter.getValue()));
    if (juce::exactlyEqual (knob.getValue(), value))
        return;
    // Sent, so the knob repaints and its tooltip follows, but not written back.
    const juce::ScopedValueSetter<bool> quiet (showing, true);
    knob.setValue (value, juce::sendNotificationSync);
}

void CompFollowAttachment::sliderDragStarted (juce::Slider*)
{
    history.beginTransaction();
    editing = true;
    grab();
    // The grab may have moved the value (followComp() moves Comp): the edit carries on from where it is now.
    {
        const juce::ScopedValueSetter<bool> quiet (showing, true);
        knob.setValue (parameter.convertFrom0to1 (parameter.getValue()), juce::sendNotificationSync);
    }
    parameter.beginChangeGesture();
}

void CompFollowAttachment::sliderDragEnded (juce::Slider*)
{
    parameter.endChangeGesture();
    editing = false;
    history.endTransaction();
}

void CompFollowAttachment::sliderValueChanged (juce::Slider*)
{
    if (showing)
        return;
    // Every edit comes inside a drag notification (KeyboardSlider): one that doesn't is a grab of its own.
    if (! editing)
    {
        const double wanted = knob.getValue();
        sliderDragStarted (&knob);
        {
            const juce::ScopedValueSetter<bool> quiet (showing, true);
            knob.setValue (wanted, juce::sendNotificationSync);
        }
        write();
        sliderDragEnded (&knob);
        return;
    }
    write();
}

void CompFollowAttachment::write()
{
    const float normalised = parameter.convertTo0to1 (static_cast<float> (knob.getValue()));
    if (! juce::exactlyEqual (normalised, parameter.getValue()))
        parameter.setValueNotifyingHost (normalised);
}

} // namespace comp1
