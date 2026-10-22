#include "Footer.h"

#include "Parameters.h"

namespace comp1
{

Footer::Footer (juce::AudioProcessorValueTreeState& parameters, plugkit::ui::Session& session)
    : globalBypassAttachment (parameters, parameters::globalBypassId, globalBypass), uiScale (session)
{
    globalBypass.setClickingTogglesState (true);
    // Its "on" is bypassed, so on it shows Off.
    globalBypass.setOffLook (true);
    globalBypass.setIconSize (14.0f);
    globalBypass.setTitle (parameters.getParameter (parameters::globalBypassId)->getName (100));
    globalBypass.setTooltip ("Global Bypass (Cmd/Ctrl+B)");
    addAndMakeVisible (globalBypass);
    addAndMakeVisible (uiScale);
    // Left to right.
    int order = 0;
    for (juce::Component* child : std::initializer_list<juce::Component*> { &globalBypass, &uiScale })
        child->setExplicitFocusOrder (++order);
}

void Footer::resized()
{
    // The buttons in a row centred down it, in from its sides.
    auto row = getLocalBounds().reduced (inset, 0);
    row = row.withSizeKeepingCentre (row.getWidth(), buttonSize);
    globalBypass.setBounds (row.removeFromLeft (buttonSize));
    uiScale.setBounds (row.removeFromRight (buttonSize));
}

} // namespace comp1
