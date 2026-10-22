#pragma once

#include "PluginProcessor.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/controls/TextChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// The Expanded View's display: its two tabs, Level and Detection EQ, as text chips along its top, and under them the
// chosen tab's content, an empty component the display's own drawing goes in (the scrolling level and Gain Reduction
// graph, #28; the Detection EQ, #29). Named "Display", and each content by its tab, as a test finds them.
//
// The chosen tab is the processor's (PluginProcessor::displayTab): a tab clicked, or pressed with Space or Return,
// chooses it, and the display follows the processor's on the editor's tick, so it shows a restored session's.
class Display final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    explicit Display (PluginProcessor& processor);

    void resized() override;

    // What a tab shows, laid out under the tabs, visible while its tab is chosen.
    juce::Component& tabContent (DisplayTab tab) { return tab == DisplayTab::level ? level : detectionEq; }
    DisplayTab shownTab() const { return shown; }

    static constexpr int tabHeight = 28, tabGap = 4, contentGap = 6;

private:
    void tick (double seconds) override;
    // Shows tab as the chosen one, with its content.
    void show (DisplayTab tab);
    staple::TextChip& chipOf (DisplayTab tab) { return tab == DisplayTab::level ? levelChip : detectionEqChip; }

    staple::TextChip levelChip { "Level", staple::TextChip::Look::plain }, detectionEqChip { "Detection EQ", staple::TextChip::Look::plain };
    PluginProcessor& processor;
    juce::Component level, detectionEq;
    DisplayTab shown = DisplayTab::level;
};

} // namespace comp1
