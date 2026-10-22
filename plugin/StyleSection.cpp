#include "StyleSection.h"

#include "Parameters.h"
#include "comp1/Style.h"

#include <algorithm>

namespace comp1
{

namespace
{
// The Style parameter's choices: each Style's name, in Style's order.
const juce::StringArray& styleNames (juce::RangedAudioParameter& style)
{
    return dynamic_cast<juce::AudioParameterChoice&> (style).choices;
}
} // namespace

StyleSection::StyleSection (PluginProcessor& p)
    : Section ("Style"), plugkit::ui::EditorTick::Client (static_cast<juce::Component&> (*this)), processor (p),
      styleParameter (*p.parameterState().getParameter (parameters::styleId))
{
    const auto& names = styleNames (styleParameter);
    const auto& offered = PluginProcessor::offeredStyles();
    for (size_t i = 0; i < offered.size(); ++i)
        picker.addItem (names[static_cast<int> (offered[i])], static_cast<int> (i) + 1);
    picker.setTitle ("Style");
    picker.setTooltip ("Style: picks the Character and writes its defaults");
    // A pick, never the tick's showing of the parameter (sent without a notification).
    picker.onChange = [this] {
        const int index = picker.getSelectedItemIndex();
        if (index < 0)
            return;
        processor.chooseStyle (PluginProcessor::offeredStyles()[static_cast<size_t> (index)]);
        showStyle();
    };
    addAndMakeVisible (picker);

    character.setTitle ("Character");
    character.setTooltip ("The Character the Style plays");
    character.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (character);
    showStyle();
}

void StyleSection::tick (double)
{
    showStyle();
}

void StyleSection::showStyle()
{
    const auto& names = styleNames (styleParameter);
    const int index = juce::roundToInt (styleParameter.convertFrom0to1 (styleParameter.getValue()));
    const auto style = static_cast<Style> (juce::jlimit (0, styleCount - 1, index));
    const auto& offered = PluginProcessor::offeredStyles();
    const auto found = std::find (offered.begin(), offered.end(), style);
    const int offeredIndex = found != offered.end() ? static_cast<int> (found - offered.begin()) : -1;
    if (offeredIndex != picker.getSelectedItemIndex())
        picker.setSelectedItemIndex (offeredIndex, juce::dontSendNotification);
    // The Characters are Style's first four, in its order (comp1/Settings.h), so each is named as its Style is.
    const auto text = "Character: " + names[static_cast<int> (characterOf (style))];
    if (character.getText() != text)
        character.setText (text, juce::dontSendNotification);
}

void StyleSection::resized()
{
    auto area = controlArea();
    picker.setBounds (area.removeFromTop (pickerHeight));
    area.removeFromTop (rowGap);
    character.setBounds (area.removeFromTop (characterHeight));
}

} // namespace comp1
