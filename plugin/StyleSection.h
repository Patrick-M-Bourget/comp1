#pragma once

#include "PluginProcessor.h"
#include "Section.h"
#include "plugkit/ui/EditorTick.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// The Style section: the Style picker, and under it the Character the Style plays, as text ("Character: FET").
//
// The picker lists offeredStyles() in their order, titled "Style". A pick, from its menu or a step of the up and down
// keys (KeyboardControl), chooses that Style (PluginProcessor::chooseStyle), so its defaults are written as one undo
// step; the picker never writes the Style parameter itself. It shows the Style parameter, followed on the editor's
// tick, so it shows a restored session's, an undo's and an A/B switch's.
class StyleSection final : public Section, private plugkit::ui::EditorTick::Client
{
public:
    explicit StyleSection (PluginProcessor& processor);

    // A fixed width, wide enough for the longest Style's name; as tall as its picker and its Character.
    int idealWidth() const override { return width; }
    int idealHeight() const override { return titleHeight + dividerGap + pickerHeight + rowGap + characterHeight; }

    void resized() override;

    static constexpr int width = 130, pickerHeight = 28, characterHeight = 20, rowGap = 6;

private:
    void tick (double seconds) override;
    // Shows the Style the parameter holds, and its Character.
    void showStyle();

    PluginProcessor& processor;
    juce::RangedAudioParameter& styleParameter;
    juce::ComboBox picker { "Style" };
    juce::Label character;
};

} // namespace comp1
