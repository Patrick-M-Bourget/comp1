#include "EditorHost.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <vector>

namespace
{

plugkit::ui::UiScaleMenu& uiScaleMenu (host::EditorHost& host) { return host.showing<plugkit::ui::UiScaleMenu> ("UI Scale"); }

// The UI Scale menu's items, in order, after its "UI Scale" title.
std::vector<juce::String> uiScaleItems (plugkit::ui::UiScaleMenu& menu)
{
    std::vector<juce::String> items;
    const auto popup = menu.menu();
    for (juce::PopupMenu::MenuItemIterator it (popup); it.next();)
        if (! it.getItem().isSectionHeader)
            items.push_back (it.getItem().text);
    return items;
}

// A pick from the footer's menu, one it offers, as the user makes it.
void pickUiScale (host::EditorHost& host, int percent)
{
    auto& menu = uiScaleMenu (host);
    const auto items = uiScaleItems (menu);
    REQUIRE (std::find (items.begin(), items.end(), juce::String (percent) + "%") != items.end());
    menu.pick (percent);
}

const auto& window = comp1::PluginProcessor::expandedWindow;

} // namespace

TEST_CASE ("The footer's UI Scale menu offers 75, 100, 125, 150 and 200%, and a new instance opens at 100% at its default size")
{
    host::EditorHost host;
    auto& menu = uiScaleMenu (host);
    CHECK (uiScaleItems (menu) == std::vector<juce::String> { "75%", "100%", "125%", "150%", "200%" });
    CHECK (menu.shownPercent() == 100);
    CHECK (host.editor->getWidth() == window.defaultSize.x);
    CHECK (host.editor->getHeight() == window.defaultSize.y);
}

TEST_CASE ("At each UI Scale the editor and its limits are their logical size times the scale")
{
    host::EditorHost host;
    const int percent = GENERATE (75, 100, 125, 150, 200);
    CAPTURE (percent);
    pickUiScale (host, percent);
    const auto scaled = [percent] (int logical) { return juce::roundToInt (logical * percent / 100.0); };
    CHECK (host.editor->getWidth() == scaled (window.defaultSize.x));
    CHECK (host.editor->getHeight() == scaled (window.defaultSize.y));
    auto* limits = host.editor->getConstrainer();
    REQUIRE (limits != nullptr);
    CHECK (limits->getMinimumWidth() == scaled (window.minimum.x));
    CHECK (limits->getMinimumHeight() == scaled (window.minimum.y));
    CHECK (limits->getMaximumWidth() == scaled (window.maximum.x));
    CHECK (limits->getMaximumHeight() == scaled (window.maximum.y));
    CHECK (uiScaleMenu (host).shownPercent() == percent);
}

TEST_CASE ("The window's size and UI Scale survive closing and reopening the editor, and are restored with the session")
{
    host::EditorHost host;
    pickUiScale (host, 150);
    host.editor->setSize (2100, 1260); // 1400 x 840 logical
    host.reopen();
    CHECK (host.editor->getWidth() == 2100);
    CHECK (host.editor->getHeight() == 1260);
    CHECK (uiScaleMenu (host).shownPercent() == 150);

    host::EditorHost other;
    host::restore (other.processor, host::saved (host.processor));
    REQUIRE (other.settleUntil ([&] { return other.editor->getWidth() == 2100; }));
    CHECK (other.editor->getHeight() == 1260);
    CHECK (uiScaleMenu (other).shownPercent() == 150);
}

TEST_CASE ("The window's size and UI Scale are in no Preset, add no undo step and make no side Modified")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    REQUIRE (session.loadPreset (session.presetState(), "Start"));
    const int steps = session.editHistory().undoSteps();

    pickUiScale (host, 125);
    host.editor->setSize (1500, 1000);
    CHECK (session.editHistory().undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());
    const auto preset = session.presetState();
    CHECK (harness::savedNamesContaining (preset, "scale").isEmpty());
    CHECK (harness::savedNamesContaining (preset, "width").isEmpty());
    CHECK (harness::savedNamesContaining (preset, "size").isEmpty());
    const auto saved = harness::savedState (host.processor);
    CHECK_FALSE (harness::savedNamesContaining (saved, "scale").isEmpty());
}
