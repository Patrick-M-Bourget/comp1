#include "Footer.h"

#include "Parameters.h"
#include "ShownAutoGain.h"
#include "comp1/Style.h"
#include "plugkit/ui/staple/Tokens.h"

#include <algorithm>

namespace comp1
{

namespace
{
// The Style chip's: the offered Styles' choices, its text and its menu items (Footer).
ChoiceChip::Config styleChip (PluginProcessor& processor, const juce::StringArray& names)
{
    // The Characters are Style's first four, in its order (comp1/Settings.h), so each is named as its Style is.
    const auto item = [names] (int choice) {
        const int character = static_cast<int> (characterOf (static_cast<Style> (choice)));
        return character == choice ? names[choice] : names[choice] + " (" + names[character] + ")";
    };
    return { .offered =
                 [] {
                     std::vector<int> offered;
                     for (const auto style : PluginProcessor::offeredStyles())
                         offered.push_back (static_cast<int> (style));
                     return offered;
                 },
             .text = [names] (int choice) { return "Style: " + names[choice]; },
             .item = item,
             .choose = [&processor] (int choice) { processor.chooseStyle (static_cast<Style> (choice)); },
             .steps = false };
}
} // namespace

AutoGainDot::AutoGainDot()
{
    setWantsKeyboardFocus (false);
    setAccessible (false);
}

void AutoGainDot::paint (juce::Graphics& g)
{
    g.setColour (staple::tokens::colour::text1);
    g.fillEllipse (getLocalBounds().toFloat().withSizeKeepingCentre (diameter, diameter));
}

Footer::Footer (PluginProcessor& p, LevelHistory& history)
    : plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      globalBypassAttachment (p.parameterState(), parameters::globalBypassId, globalBypass),
      style (parameters::parameterOf (p.parameterState(), parameters::styleId),
             p.session().editHistory(),
             styleChip (p, parameters::parameterOf (p.parameterState(), parameters::styleId).getAllValueStrings())),
      processingModeItem (processingModeReading (p), p), stereoItem (stereoReading (p), p), saturationItem (saturationReading (p), p),
      outputItem (outputReading (p), p, history), uiScale (p.session())
{
    globalBypass.setClickingTogglesState (true);
    // Its "on" is bypassed, so on it shows Off.
    globalBypass.setOffLook (true);
    globalBypass.setIconSize (14.0f);
    globalBypass.setTitle (parameters::parameterOf (p.parameterState(), parameters::globalBypassId).getName (100));
    globalBypass.setTooltip ("Global Bypass (Cmd/Ctrl+B)");
    // Left to right.
    int order = 0;
    for (juce::Component* child :
         std::initializer_list<juce::Component*> { &globalBypass, &style, &processingModeItem, &stereoItem, &saturationItem, &outputItem, &uiScale })
    {
        addAndMakeVisible (*child);
        child->setExplicitFocusOrder (++order);
    }
    // Last, one of the two showing at a time, in the same place in Tab's order.
    ++order;
    for (auto* button : { &toCompact, &toExpanded })
    {
        button->setTitle (button->getName());
        button->setTooltip (button->getName());
        button->setExplicitFocusOrder (order);
        button->onClick = [this] {
            if (onSwitchView != nullptr)
                onSwitchView();
        };
        addChildComponent (*button);
    }
    showSwitchFor (p.view());

    addChildComponent (dot);
    outputItem.onTextChanged = [this] { resized(); };
    tick (0.0);
}

Footer::~Footer() = default;

void Footer::tick (double)
{
    const auto amount = shownAutoGain (processor);
    if (amount.has_value())
        if (const auto tooltip = "Auto Gain: " + *amount; tooltip != dot.getTooltip())
            dot.setTooltip (tooltip);
    if (const bool shown = amount.has_value(); shown != dot.isVisible())
        dot.setVisible (shown);
}

void Footer::showSwitchFor (View shown)
{
    auto& showing = shown == View::compact ? toExpanded : toCompact;
    auto& hiding = &showing == &toCompact ? toExpanded : toCompact;
    if (showing.isVisible() && ! hiding.isVisible())
        return;
    const bool focused = hiding.hasKeyboardFocus (false);
    showing.setVisible (true);
    hiding.setVisible (false);
    if (focused)
        showing.grabKeyboardFocus();
}

void Footer::resized()
{
    // The buttons in a row centred down it, in from its sides, each chip as wide as its longest choice.
    auto row = getLocalBounds().reduced (inset, 0);
    row = row.withSizeKeepingCentre (row.getWidth(), buttonSize);
    globalBypass.setBounds (row.removeFromLeft (buttonSize));
    const auto switchArea = row.removeFromRight (buttonSize);
    toCompact.setBounds (switchArea);
    toExpanded.setBounds (switchArea);
    row.removeFromRight (chipGap);
    uiScale.setBounds (row.removeFromRight (buttonSize));
    // The gap left of the UI Scale menu holds the Auto Gain dot, so the readout doesn't move as it shows.
    dot.setBounds (row.removeFromRight (chipGap));
    outputItem.setBounds (row.removeFromRight (std::max (outputWidth, outputItem.idealWidth())));
    row.removeFromRight (chipGap);
    row.removeFromLeft (chipGap);
    style.setBounds (row.removeFromLeft (style.widestIdealWidth()));
    row.removeFromLeft (chipGap);
    processingModeItem.setBounds (row.removeFromLeft (processingModeItem.idealWidth()));
    row.removeFromLeft (chipGap);
    // What the chip and the Processing Mode readout leave holds the Stereo readout then the Saturation readout, a gap apart: at full length where it
    // can, else Stereo shrunk to its label, else both.
    const int fullStereo = stereoItem.idealWidth(), fullSaturation = saturationItem.idealWidth();
    const int stereoWidth = row.getWidth() >= fullStereo + chipGap + fullSaturation ? fullStereo : stereoItem.shrunkWidth();
    const int saturationWidth = row.getWidth() >= stereoWidth + chipGap + fullSaturation ? fullSaturation : saturationItem.shrunkWidth();
    saturationItem.setBounds (row.removeFromRight (saturationWidth));
    row.removeFromRight (chipGap);
    stereoItem.setBounds (row.removeFromRight (stereoWidth));
}

} // namespace comp1
