#pragma once

#include "LevelGraph.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "TransferCurveGraph.h"
#include "TransferCurveInset.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/controls/TextChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace comp1
{

// The Expanded View's display: its two tabs, Level and Transfer Curve, as text chips along its top, and under them the
// chosen tab's content: on Level, the Level and Gain Reduction Graph (LevelGraph), fed from the editor's level history;
// on Transfer Curve, the Transfer Curve Graph (TransferCurveGraph), fed from the level history too. The Transfer Curve tab's place is provisional (#94).
// While the Level tab shows, the Transfer Curve inset (TransferCurveInset) sits over the Level graph's top-left corner,
// insetGap in from it, over its oldest audio: a Tab stop after the tabs, whose Threshold and Ratio handles take the mouse.
// Named "Display", and each content by its tab, as a test finds them.
//
// The chosen tab is the processor's (PluginProcessor::displayTab): a tab clicked, or pressed with Space or Return,
// chooses it, and the display follows the processor's on the editor's tick, so it shows a restored session's.
class Display final : public juce::Component, private plugkit::ui::EditorTick::Client
{
public:
    Display (PluginProcessor& processor, LevelHistory& history);

    void resized() override;

    // What a tab shows, laid out under the tabs, visible while its tab is chosen.
    juce::Component& tabContent (DisplayTab tab);
    DisplayTab shownTab() const { return shown; }
    LevelGraph& levelGraph() { return level; }
    // What fills the Transfer Curve tab's content.
    TransferCurveGraph& transferCurveGraph() { return transferCurve; }
    // Over the Level tab's content, shown with it.
    TransferCurveInset& transferCurveInset() { return inset; }

    static constexpr int tabHeight = 28, tabGap = 4, contentGap = 6, insetGap = 8;

private:
    void tick (double seconds) override;
    // Shows tab as the chosen one, with its content.
    void show (DisplayTab tab);
    staple::TextChip& chipOf (DisplayTab tab);

    // The tabs, left to right.
    static constexpr DisplayTab allTabs[] { DisplayTab::level, DisplayTab::transferCurve };

    staple::TextChip levelChip { "Level", staple::TextChip::Look::plain }, transferCurveChip { "Transfer Curve", staple::TextChip::Look::plain };
    PluginProcessor& processor;
    LevelGraph level;
    DisplayTab shown = DisplayTab::level;
    TransferCurveGraph transferCurve;
    TransferCurveInset inset;
};

} // namespace comp1
