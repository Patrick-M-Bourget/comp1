#include "PluginEditor.h"

#include "Parameters.h"

namespace comp1
{

namespace
{
// The footer's buttons, in a row centred down it, 6 px in from its sides.
constexpr int footerButtonSize = 28, footerInset = 6;
} // namespace

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
    auto row = getLocalBounds().reduced (footerInset, 0);
    row = row.withSizeKeepingCentre (row.getWidth(), footerButtonSize);
    globalBypass.setBounds (row.removeFromLeft (footerButtonSize));
    uiScale.setBounds (row.removeFromRight (footerButtonSize));
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : EditorShell (p, p.session(), { "comp1", parameters::globalBypassId }), footer (p.parameterState(), p.session())
{
    footer.uiScaleMenu().onPicked = [this] (int) { applyUiScale(); };
    // Along the bottom, last in Tab's order.
    open (footer);
}

void PluginEditor::layOutBody (juce::Rectangle<int>) {}

} // namespace comp1
