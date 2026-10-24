#pragma once

#include "LevelGraph.h"
#include "LevelHistory.h"
#include "PluginProcessor.h"
#include "SpectrumDisplay.h"
#include "TransferCurveGraph.h"
#include "TransferCurveInset.h"
#include "plugkit/ui/EditorTick.h"
#include "plugkit/ui/staple/controls/TextChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <iterator>

namespace comp1
{

// The Expanded View's display: its three tabs, Level, Transfer Curve and Spectrum, as text chips along its top, and under
// them the chosen tab's content: on Level, the Level and Gain Reduction Graph (LevelGraph), fed from the editor's level
// history; on Transfer Curve, the Transfer Curve Graph (TransferCurveGraph), fed from the level history too; on
// Spectrum, the Spectrum display (SpectrumDisplay), fed from the Engine's input and output taps. The Transfer Curve
// tab's place is provisional.
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
    // What fills the Spectrum tab's content.
    SpectrumDisplay& spectrumDisplay() { return spectrum; }
    // Over the Level tab's content, shown with it.
    TransferCurveInset& transferCurveInset() { return inset; }

    static constexpr int tabHeight = 28, tabGap = 4, contentGap = 6, insetGap = 8;

private:
    void tick (double seconds) override;
    // Shows tab as the chosen one, with its content.
    void show (DisplayTab tab);

    // A display tab's row in tabs: the tab, its chip, labelled with the tab's name, and what the tab shows.
    struct Tab
    {
        DisplayTab tab;
        staple::TextChip chip;
        juce::Component& content;
    };

    PluginProcessor& processor;
    LevelGraph level;
    DisplayTab shown = DisplayTab::level;
    TransferCurveGraph transferCurve;
    TransferCurveInset inset;
    SpectrumDisplay spectrum;
    // The tabs, left to right: a row for each of displayTabs, in its order, each at its tab's rowOf. A chip chooses
    // its own row's tab, so a row out of place shows the wrong content for its chip.
    std::array<Tab, std::size (displayTabs)> tabs { {
        { DisplayTab::level, staple::TextChip { "Level", staple::TextChip::Look::plain }, level },
        { DisplayTab::transferCurve, staple::TextChip { "Transfer Curve", staple::TextChip::Look::plain }, transferCurve },
        { DisplayTab::spectrum, staple::TextChip { "Spectrum", staple::TextChip::Look::plain }, spectrum },
    } };
};

} // namespace comp1
