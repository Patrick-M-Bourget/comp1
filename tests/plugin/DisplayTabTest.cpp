#include "EditorHost.h"
#include "SpectrumDisplay.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <vector>

namespace
{

using comp1::DisplayTab;

juce::Button& tabChip (host::EditorHost& host, const juce::String& title) { return host.showing<juce::Button> (title); }

// The tab the editor shows: its chip on, its content alone showing.
void checkShows (host::EditorHost& host, DisplayTab tab)
{
    auto& display = host.expandedView().display();
    CHECK (display.shownTab() == tab);
    CHECK (tabChip (host, "Level").getToggleState() == (tab == DisplayTab::level));
    CHECK (tabChip (host, "Spectrum").getToggleState() == (tab == DisplayTab::spectrum));
    CHECK (display.tabContent (DisplayTab::level).isShowing() == (tab == DisplayTab::level));
    CHECK (display.tabContent (DisplayTab::spectrum).isShowing() == (tab == DisplayTab::spectrum));
}

// A click on the tab titled title, which shows tab: its onClick at once, as a click runs it.
void click (host::EditorHost& host, const juce::String& title, DisplayTab tab)
{
    auto& chip = tabChip (host, title);
    REQUIRE (chip.onClick != nullptr);
    chip.onClick();
    REQUIRE (host.processor.displayTab() == tab);
}

} // namespace

TEST_CASE ("The display opens on Level, its tabs Level and Spectrum only, and a click on a tab or Space on it shows that tab")
{
    host::EditorHost host;
    CHECK (host.processor.displayTab() == DisplayTab::level);
    checkShows (host, DisplayTab::level);
    // No Detection EQ or Transfer Curve tab: the Level tab's Transfer Curve inset is the Transfer Curve's view.
    CHECK (host.findAll<juce::Button> ([] (juce::Button& b) { return b.getTitle() == "Detection EQ" || b.getTitle() == "Transfer Curve"; }).empty());
    CHECK (std::size (comp1::displayTabs) == 2);

    click (host, "Spectrum", DisplayTab::spectrum);
    checkShows (host, DisplayTab::spectrum);

    // Space on the focused tab, as the keyboard picks one.
    tabChip (host, "Level").grabKeyboardFocus();
    CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.displayTab() == DisplayTab::level);
    checkShows (host, DisplayTab::level);
    tabChip (host, "Spectrum").grabKeyboardFocus();
    CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.displayTab() == DisplayTab::spectrum);
    checkShows (host, DisplayTab::spectrum);
}

TEST_CASE ("The chosen tab is saved with the session and restored, and a session saved without it opens on Level")
{
    host::EditorHost host;
    click (host, "Spectrum", DisplayTab::spectrum);
    const auto saved = host::saved (host.processor);
    CHECK_FALSE (harness::savedNamesContaining (harness::savedState (saved), "display_tab").isEmpty());

    // Into a new instance, opened afterwards.
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.displayTab() == DisplayTab::spectrum);
    other.reopen();
    checkShows (other, DisplayTab::spectrum);

    // Into an open editor, which follows it.
    click (host, "Level", DisplayTab::level);
    host::restore (host.processor, saved);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::spectrum; }));
    checkShows (host, DisplayTab::spectrum);

    // Without it.
    auto xml = harness::savedXml (saved);
    REQUIRE (xml != nullptr);
    xml->removeAttribute ("display_tab");
    host::restore (host.processor, *xml);
    CHECK (host.processor.displayTab() == DisplayTab::level);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::level; }));
    // And it is saved again only as the instance holds it.
    CHECK (harness::savedState (host.processor).getProperty ("display_tab") == "level");

}

TEST_CASE ("The chosen tab isn't in a Preset, isn't an undo step and makes no side Modified, and loading a Preset or undoing leaves it alone")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    click (host, "Spectrum", DisplayTab::spectrum);
    CHECK (harness::savedNamesContaining (session.presetState(), "display").isEmpty());
    CHECK (history.undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());

    // An edit and its undo.
    auto& threshold = host.parameter (comp1::parameters::thresholdId);
    threshold.beginChangeGesture();
    threshold.setValueNotifyingHost (threshold.convertTo0to1 (-30.0f));
    threshold.endChangeGesture();
    REQUIRE (history.canUndo());
    click (host, "Level", DisplayTab::level);
    history.undo();
    CHECK (host.processor.displayTab() == DisplayTab::level);

    // A Preset loaded.
    click (host, "Spectrum", DisplayTab::spectrum);
    REQUIRE (session.loadPreset (preset, "Start"));
    host.settle();
    CHECK (host.processor.displayTab() == DisplayTab::spectrum);
    checkShows (host, DisplayTab::spectrum);
}

TEST_CASE ("A session saved on a retired tab, Transfer Curve or Detection EQ, or with an unknown one, opens on Level, and is saved again as level")
{
    host::EditorHost host;
    click (host, "Spectrum", DisplayTab::spectrum);
    auto xml = harness::savedXml (host::saved (host.processor));
    REQUIRE (xml != nullptr);
    for (const auto* name : { "transfer_curve", "detection_eq", "waveform" })
    {
        CAPTURE (name);
        // Into an open editor on Spectrum, which follows it.
        click (host, "Spectrum", DisplayTab::spectrum);
        xml->setAttribute ("display_tab", name);
        host::restore (host.processor, *xml);
        CHECK (host.processor.displayTab() == DisplayTab::level);
        REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::level; }));
        checkShows (host, DisplayTab::level);
        CHECK (harness::savedState (host.processor).getProperty ("display_tab") == "level");

        // Into a new instance, opened afterwards.
        host::EditorHost other;
        host::restore (other.processor, *xml);
        CHECK (other.processor.displayTab() == DisplayTab::level);
        other.reopen();
        checkShows (other, DisplayTab::level);
    }
}

TEST_CASE ("The Spectrum tab, after Level: a click shows it, saved as spectrum and restored, in no Preset or undo step, and not in the Compact View")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    auto& chip = tabChip (host, "Spectrum");
    CHECK (chip.getTooltip() == "Show the Spectrum display");
    CHECK (chip.getX() > tabChip (host, "Level").getRight());
    CHECK (host.expandedView().display().tabContent (DisplayTab::spectrum).getTitle() == "Spectrum");

    click (host, "Spectrum", DisplayTab::spectrum);
    checkShows (host, DisplayTab::spectrum);
    CHECK (harness::savedNamesContaining (session.presetState(), "display").isEmpty());
    CHECK (history.undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());

    // Saved as spectrum, into a new instance and into an open editor.
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedState (saved).getProperty ("display_tab") == "spectrum");
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.displayTab() == DisplayTab::spectrum);
    other.reopen();
    checkShows (other, DisplayTab::spectrum);
    click (host, "Level", DisplayTab::level);
    host::restore (host.processor, saved);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::spectrum; }));
    checkShows (host, DisplayTab::spectrum);

    // The Compact View has no display, so no Spectrum tab.
    host.processor.setView (comp1::View::compact);
    host.reopen();
    CHECK (host.findAll<juce::Button> ([] (juce::Button& b) { return b.getTitle() == "Spectrum" && b.isShowing(); }).empty());
    CHECK (host.findAll<comp1::SpectrumDisplay> ([] (comp1::SpectrumDisplay& d) { return d.isShowing(); }).empty());
}

TEST_CASE ("The display's tab chips, left to right, are displayTabs' rows in order, each choosing its row's tab and showing that tab's content")
{
    host::EditorHost host;
    auto& display = host.expandedView().display();
    std::vector<juce::Button*> chips;
    for (auto* child : display.getChildren())
        if (auto* chip = dynamic_cast<juce::Button*> (child))
            chips.push_back (chip);
    std::sort (chips.begin(), chips.end(), [] (const juce::Button* a, const juce::Button* b) { return a->getX() < b->getX(); });
    REQUIRE (chips.size() == std::size (comp1::displayTabs));
    for (std::size_t row = 0; row < chips.size(); ++row)
    {
        CAPTURE (row);
        REQUIRE (chips[row]->onClick != nullptr);
        chips[row]->onClick();
        const auto tab = comp1::displayTabs[row].tab;
        CHECK (host.processor.displayTab() == tab);
        CHECK (chips[row]->getToggleState());
        CHECK (display.tabContent (tab).isShowing());
    }
}
