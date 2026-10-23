#include "EditorHost.h"
#include "SpectrumDisplay.h"

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
    CHECK (tabChip (host, "Transfer Curve").getToggleState() == (tab == DisplayTab::transferCurve));
    CHECK (tabChip (host, "Spectrum").getToggleState() == (tab == DisplayTab::spectrum));
    CHECK (display.tabContent (DisplayTab::level).isShowing() == (tab == DisplayTab::level));
    CHECK (display.tabContent (DisplayTab::transferCurve).isShowing() == (tab == DisplayTab::transferCurve));
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

TEST_CASE ("The display opens on Level, its tabs Level, Transfer Curve and Spectrum, and a click on a tab shows that tab")
{
    host::EditorHost host;
    CHECK (host.processor.displayTab() == DisplayTab::level);
    checkShows (host, DisplayTab::level);
    // No Detection EQ tab.
    CHECK (host.findAll<juce::Button> ([] (juce::Button& b) { return b.getTitle() == "Detection EQ"; }).empty());

    click (host, "Transfer Curve", DisplayTab::transferCurve);
    checkShows (host, DisplayTab::transferCurve);

    // Space on the focused tab, as the keyboard picks one.
    tabChip (host, "Level").grabKeyboardFocus();
    CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.displayTab() == DisplayTab::level);
    checkShows (host, DisplayTab::level);
}

TEST_CASE ("The chosen tab is saved with the session and restored, and a session saved without it opens on Level")
{
    host::EditorHost host;
    click (host, "Transfer Curve", DisplayTab::transferCurve);
    const auto saved = host::saved (host.processor);
    CHECK_FALSE (harness::savedNamesContaining (harness::savedState (saved), "display_tab").isEmpty());

    // Into a new instance, opened afterwards.
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.displayTab() == DisplayTab::transferCurve);
    other.reopen();
    checkShows (other, DisplayTab::transferCurve);

    // Into an open editor, which follows it.
    click (host, "Level", DisplayTab::level);
    host::restore (host.processor, saved);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::transferCurve; }));
    checkShows (host, DisplayTab::transferCurve);

    // Without it.
    auto xml = harness::savedXml (saved);
    REQUIRE (xml != nullptr);
    xml->removeAttribute ("display_tab");
    host::restore (host.processor, *xml);
    CHECK (host.processor.displayTab() == DisplayTab::level);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::level; }));
    // And it is saved again only as the instance holds it.
    CHECK (harness::savedState (host.processor).getProperty ("display_tab") == "level");

    // An older session's Detection EQ tab, which the display no longer has: Level.
    click (host, "Transfer Curve", DisplayTab::transferCurve);
    xml->setAttribute ("display_tab", "detection_eq");
    host::restore (host.processor, *xml);
    CHECK (host.processor.displayTab() == DisplayTab::level);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::level; }));
    checkShows (host, DisplayTab::level);
}

TEST_CASE ("The chosen tab isn't in a Preset, isn't an undo step and makes no side Modified, and loading a Preset or undoing leaves it alone")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    click (host, "Transfer Curve", DisplayTab::transferCurve);
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
    click (host, "Transfer Curve", DisplayTab::transferCurve);
    REQUIRE (session.loadPreset (preset, "Start"));
    host.settle();
    CHECK (host.processor.displayTab() == DisplayTab::transferCurve);
    checkShows (host, DisplayTab::transferCurve);
}

TEST_CASE ("The Transfer Curve tab: a click or Space shows it, saved as transfer_curve and restored, in no Preset or undo step")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    // Its chip, after Level's, titled and tooltipped as it.
    auto& chip = tabChip (host, "Transfer Curve");
    CHECK (chip.getTooltip() == "Show the Transfer Curve display");
    CHECK (chip.getX() > tabChip (host, "Level").getRight());
    CHECK (host.expandedView().display().tabContent (DisplayTab::transferCurve).getTitle() == "Transfer Curve Graph");

    click (host, "Transfer Curve", DisplayTab::transferCurve);
    checkShows (host, DisplayTab::transferCurve);
    CHECK (harness::savedNamesContaining (session.presetState(), "display").isEmpty());
    CHECK (history.undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());

    // Space on the focused tab.
    click (host, "Level", DisplayTab::level);
    chip.grabKeyboardFocus();
    CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.displayTab() == DisplayTab::transferCurve);
    checkShows (host, DisplayTab::transferCurve);

    // Saved as transfer_curve, into a new instance and into an open editor.
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedState (saved).getProperty ("display_tab") == "transfer_curve");
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.displayTab() == DisplayTab::transferCurve);
    other.reopen();
    checkShows (other, DisplayTab::transferCurve);
    click (host, "Level", DisplayTab::level);
    host::restore (host.processor, saved);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == DisplayTab::transferCurve; }));
    checkShows (host, DisplayTab::transferCurve);

    // An unknown value opens on Level.
    auto xml = harness::savedXml (saved);
    REQUIRE (xml != nullptr);
    xml->setAttribute ("display_tab", "waveform");
    host::restore (host.processor, *xml);
    CHECK (host.processor.displayTab() == DisplayTab::level);
}

TEST_CASE ("The Spectrum tab, after Transfer Curve: a click shows it, saved as spectrum and restored, in no Preset or undo step, and not in the Compact View")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    auto& chip = tabChip (host, "Spectrum");
    CHECK (chip.getTooltip() == "Show the Spectrum display");
    CHECK (chip.getX() > tabChip (host, "Transfer Curve").getRight());
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
