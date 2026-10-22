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

juce::ComboBox& stylePicker (host::EditorHost& host) { return host.showing<juce::ComboBox> ("Style"); }

// What the picker shows, as the user reads it.
juce::String shownStyle (host::EditorHost& host) { return stylePicker (host).getText(); }

// A pick from the picker's menu, as the user makes it.
void pick (host::EditorHost& host, const juce::String& style)
{
    auto& picker = stylePicker (host);
    for (int i = 0; i < picker.getNumItems(); ++i)
        if (picker.getItemText (i) == style)
        {
            picker.setSelectedItemIndex (i, juce::sendNotificationSync);
            return;
        }
    FAIL ("The Style picker doesn't offer " << style);
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

void edit (host::EditorHost& host, const juce::String& id, float plain)
{
    auto& parameter = host.parameter (id);
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (plain));
    parameter.endChangeGesture();
}

} // namespace

TEST_CASE ("The Style picker lists the eight Styles in the frozen order, showing the Style and the Character it sets")
{
    host::EditorHost host;
    auto& picker = stylePicker (host);
    juce::StringArray items;
    for (int i = 0; i < picker.getNumItems(); ++i)
        items.add (picker.getItemText (i));
    CHECK (items == juce::StringArray { "Clean", "VCA", "FET", "Opto", "Vocal", "Instrument", "Bus", "Drums" });
    CHECK (shownStyle (host) == "Clean");
    CHECK (host.showing<juce::Label> ("Character").getText() == "Character: Clean");
}

TEST_CASE ("Picking a Style chooses it: its defaults are written as one undo step, and undo puts everything back")
{
    host::EditorHost host;
    auto& history = host.processor.session().editHistory();
    edit (host, parameters::thresholdId, -40.0f);
    edit (host, parameters::compFollowId, 0.0f);
    edit (host, parameters::autoGainId, 1.0f);
    const auto before = allValues (host);
    const int steps = history.undoSteps();

    pick (host, "Drums");
    CHECK (host.value (parameters::styleId) == static_cast<float> (Style::Drums));
    // Drums' defaults (comp1/Style.h): Comp Follow on, 5 ms Attack and 50 ms Release; Auto Gain untouched.
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK_THAT (host.value (parameters::attackId), WithinRel (5.0f, 1.0e-5f));
    CHECK_THAT (host.value (parameters::releaseId), WithinRel (50.0f, 1.0e-5f));
    CHECK_THAT (host.value (parameters::thresholdId), WithinRel (-18.0f, 1.0e-5f));
    CHECK (host.value (parameters::autoGainId) == 1.0f);
    CHECK (history.undoSteps() == steps + 1);
    REQUIRE (host.settleUntil ([&] { return host.showing<juce::Label> ("Character").getText() == "Character: FET"; }));

    history.undo();
    CHECK (allValues (host) == before);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Clean"; }));
}

TEST_CASE ("Up and down step the focused Style picker through the Styles, each step choosing that Style as one undo step")
{
    host::EditorHost host;
    auto& history = host.processor.session().editHistory();
    auto& picker = stylePicker (host);
    picker.grabKeyboardFocus();
    const int steps = history.undoSteps();

    CHECK (host.press (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (host.value (parameters::styleId) == static_cast<float> (Style::VCA));
    CHECK (host.value (parameters::compFollowId) == 1.0f);
    CHECK (host.press (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (host.value (parameters::styleId) == static_cast<float> (Style::FET));
    // FET's 0.5 ms Attack.
    CHECK_THAT (host.value (parameters::attackId), WithinRel (0.5f, 1.0e-5f));
    CHECK (history.undoSteps() == steps + 2);
    CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (host.value (parameters::styleId) == static_cast<float> (Style::VCA));
    CHECK (history.undoSteps() == steps + 3);
}

TEST_CASE ("The Style picker follows a Style changed by a restored session, undo and an A/B switch")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    pick (host, "Bus");
    const auto savedOnBus = host::saved (host.processor);

    // Undo.
    session.editHistory().undo();
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Clean"; }));

    // A restored session.
    host::restore (host.processor, savedOnBus);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Bus"; }));
    CHECK (host.showing<juce::Label> ("Character").getText() == "Character: VCA");

    // An A/B switch: B, a copy of A, then picked to Vocal, then back to A.
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    pick (host, "Vocal");
    session.selectCompareSide (plugkit::ui::CompareSide::A);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Bus"; }));
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    REQUIRE (host.settleUntil ([&] { return shownStyle (host) == "Vocal"; }));
}
