#include "EditorHost.h"

#include <catch2/catch_test_macros.hpp>

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
    CHECK (tabChip (host, "Detection EQ").getToggleState() == (tab == DisplayTab::detectionEq));
    CHECK (display.tabContent (DisplayTab::level).isShowing() == (tab == DisplayTab::level));
    CHECK (display.tabContent (DisplayTab::detectionEq).isShowing() == (tab == DisplayTab::detectionEq));
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

TEST_CASE ("The display opens on Level, and a click on a tab shows that tab")
{
    host::EditorHost host;
    CHECK (host.processor.displayTab() == DisplayTab::level);
    checkShows (host, DisplayTab::level);

    click (host, "Detection EQ", DisplayTab::detectionEq);
    checkShows (host, DisplayTab::detectionEq);

    // Space on the focused tab, as the keyboard picks one.
    tabChip (host, "Level").grabKeyboardFocus();
    CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.displayTab() == DisplayTab::level);
    checkShows (host, DisplayTab::level);
}

TEST_CASE ("The chosen tab is saved with the session and restored, and a session saved without it opens on Level")
{
    host::EditorHost host;
    click (host, "Detection EQ", DisplayTab::detectionEq);
    const auto saved = host::saved (host.processor);
    CHECK_FALSE (harness::savedNamesContaining (harness::savedState (saved), "display_tab").isEmpty());

    // Into a new instance, opened afterwards.
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.displayTab() == DisplayTab::detectionEq);
    other.reopen();
    checkShows (other, DisplayTab::detectionEq);

    // Into an open editor, which follows it.
    click (host, "Level", DisplayTab::level);
    host::restore (host.processor, saved);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::detectionEq; }));
    checkShows (host, DisplayTab::detectionEq);

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

    click (host, "Detection EQ", DisplayTab::detectionEq);
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
    click (host, "Detection EQ", DisplayTab::detectionEq);
    REQUIRE (session.loadPreset (preset, "Start"));
    host.settle();
    CHECK (host.processor.displayTab() == DisplayTab::detectionEq);
    checkShows (host, DisplayTab::detectionEq);
}
