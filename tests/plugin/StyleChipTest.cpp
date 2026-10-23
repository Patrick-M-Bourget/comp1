// The footer's Style chip: its menu of the eight Styles with their Characters, a pick choosing a Style as one undo step,
// its keys, and its following a Style changed elsewhere.

#include "EditorHost.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <map>

using Catch::Matchers::WithinRel;

namespace
{

using comp1::Style;
namespace parameters = comp1::parameters;

comp1::ChoiceChip& styleChip (host::EditorHost& host) { return host.showing<comp1::ChoiceChip> ("Style"); }

// What the chip shows, as the user reads it.
juce::String shownStyle (host::EditorHost& host) { return styleChip (host).getButtonText(); }

// What a screen reader reads as its value.
juce::String spokenStyle (host::EditorHost& host)
{
    auto* handler = styleChip (host).getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getValueInterface() != nullptr);
    return handler->getValueInterface()->getCurrentValueAsString();
}

// A pick from the chip's menu, as the user makes it.
void pick (host::EditorHost& host, const juce::String& item)
{
    auto action = harness::actionOf (styleChip (host).menu(), item);
    INFO ("The Style chip doesn't offer " << item);
    REQUIRE (action != nullptr);
    action();
}

// Every host parameter's plain value, by ID.
std::map<juce::String, float> allValues (host::EditorHost& host)
{
    std::map<juce::String, float> values;
    for (auto* p : host.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            values[ranged->getParameterID()] = host.value (ranged->getParameterID());
    return values;
}

} // namespace

TEST_CASE ("The Style chip's menu lists the eight Styles in the frozen order with their Characters, the Style shown ticked; the chip reads Style: and the Style")
{
    host::EditorHost host;
    juce::StringArray items;
    juce::StringArray ticked;
    const auto menu = styleChip (host).menu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        items.add (it.getItem().text);
        if (it.getItem().isTicked)
            ticked.add (it.getItem().text);
    }
    // A Character's own name alone; a Source Style's with the Character it plays.
    CHECK (items == juce::StringArray { "Clean", "VCA", "FET", "Opto", "Vocal (Clean)", "Instrument (Opto)", "Bus (VCA)", "Drums (FET)" });
    CHECK (ticked == juce::StringArray { "Clean" });
    CHECK (shownStyle (host) == "Style: Clean");
    CHECK (spokenStyle (host) == "Clean");
}

TEST_CASE ("Picking a Style from the chip chooses it: its defaults are written as one undo step, and undo puts everything back")
{
    host::EditorHost host;
    auto& history = host.processor.session().editHistory();
    host.edit (parameters::thresholdId, -40.0f);
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::autoGainId, 1.0f);
    const auto before = allValues (host);
    const int steps = history.undoSteps();

    pick (host, "Drums (FET)");
    CHECK (host.value (parameters::styleId) == static_cast<float> (Style::Drums));
    // Drums' defaults (comp1/Style.h): Comp Follow on, 5 ms Attack and 50 ms Release, Auto Gain on.
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::attackId), WithinRel (5.0f, 1.0e-5f));
    CHECK_THAT (host.value (parameters::releaseId), WithinRel (50.0f, 1.0e-5f));
    CHECK_THAT (host.value (parameters::thresholdId), WithinRel (-18.0f, 1.0e-5f));
    CHECK (host.value (parameters::autoGainId) == 1.0f);
    CHECK (history.undoSteps() == steps + 1);
    CHECK (shownStyle (host) == "Style: Drums");
    CHECK (spokenStyle (host) == "Drums (FET)");

    history.undo();
    CHECK (allValues (host) == before);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Clean"; }));
}

TEST_CASE ("Up and down on the focused Style chip change nothing; Return and Space open its menu")
{
    host::EditorHost host;
    auto& history = host.processor.session().editHistory();
    auto& chip = styleChip (host);
    chip.grabKeyboardFocus();
    REQUIRE (chip.hasKeyboardFocus (false));
    const auto before = allValues (host);
    const int steps = history.undoSteps();

    host.press (juce::KeyPress (juce::KeyPress::downKey));
    host.press (juce::KeyPress (juce::KeyPress::upKey));
    host.press (juce::KeyPress (juce::KeyPress::upKey));
    host.settle();
    CHECK (allValues (host) == before);
    CHECK (history.undoSteps() == steps);
    CHECK (harness::openMenu() == nullptr);

    const auto key = GENERATE (juce::KeyPress::returnKey, juce::KeyPress::spaceKey);
    CAPTURE (key);
    CHECK (host.press (juce::KeyPress (key)));
    REQUIRE (host.settleUntil ([] { return harness::openMenu() != nullptr; }));
    juce::PopupMenu::dismissAllActiveMenus();
    CHECK (allValues (host) == before);
}

TEST_CASE ("The Style chip follows a Style changed by undo, a restored session, a loaded Preset and an A/B switch")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    pick (host, "Bus (VCA)");
    const auto savedOnBus = host::saved (host.processor);
    const auto busPreset = session.presetState();

    // Undo.
    session.editHistory().undo();
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Clean"; }));

    // A restored session.
    host::restore (host.processor, savedOnBus);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Bus"; }));
    CHECK (spokenStyle (host) == "Bus (VCA)");

    // A loaded Preset.
    pick (host, "Opto");
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Opto"; }));
    REQUIRE (session.loadPreset (busPreset, "Bus"));
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Bus"; }));

    // An A/B switch: B, a copy of A, then picked to Vocal, then back to A.
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    pick (host, "Vocal (Clean)");
    session.selectCompareSide (plugkit::ui::CompareSide::A);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Bus"; }));
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Style: Vocal"; }));
}

TEST_CASE ("The Style chip is in the footer of both views, after Global Bypass and before Processing Mode, and neither view has a Style section")
{
    host::EditorHost host;
    const auto view = GENERATE (comp1::View::expanded, comp1::View::compact);
    CAPTURE (static_cast<int> (view));
    if (view == comp1::View::compact)
    {
        host::EditorHost::click (host.expandedView().viewSwitch());
        REQUIRE (host.expandedView().shownView() == comp1::View::compact);
    }
    auto& footer = host.footer();
    auto& chip = styleChip (host);
    CHECK (footer.isParentOf (&chip));
    auto& bypass = host.showing<juce::Button> ("Global Bypass");
    auto& processingMode = host.showing<comp1::ChoiceChip> ("Processing Mode");
    CHECK (bypass.getRight() < chip.getX());
    CHECK (chip.getRight() < processingMode.getX());
    CHECK (bypass.getExplicitFocusOrder() < chip.getExplicitFocusOrder());
    CHECK (chip.getExplicitFocusOrder() < processingMode.getExplicitFocusOrder());
    // Wide enough for its longest text, whichever it shows.
    CHECK (chip.getWidth() == chip.widestIdealWidth());
    CHECK (host.findAll<comp1::Section> ([] (comp1::Section& s) { return s.isShowing() && s.getName() == "Style"; }).empty());
}
