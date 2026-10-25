// The footer's Saturation readout, in both views: what it reads, Drive, and its dimming while Saturation is off; its
// place in the footer; the Saturation popover it opens, whose controls drive Saturation's parameters; and the Expanded
// View's Character section, which no longer holds them.

#include "EditorHost.h"
#include "SaturationPopover.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace parameters = comp1::parameters;

namespace
{

// Shows the Compact View when compact, through the view switch.
void showView (host::EditorHost& host, bool compact)
{
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
}

juce::Component* focused() { return juce::Component::getCurrentlyFocusedComponent(); }

// The Saturation popover's controls, in Tab's order: each titled with its host parameter's name.
const juce::StringArray popoverControls { "Saturation", "Saturation Position", "Drive" };

} // namespace

TEST_CASE ("The Saturation readout reads SAT and Drive in whole dB, signed, in both views, dimmed while Saturation is off")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    showView (host, compact);
    auto& readout = host.saturationReadout();
    REQUIRE (readout.isShowing());
    CHECK (readout.getTitle() == "Saturation");
    const auto reads = [&] (const juce::String& text) { return host.settleUntil ([&] { return readout.getButtonText() == text; }); };

    host.edit (parameters::saturationId, 1.0f);
    host.edit (parameters::driveId, 6.0f);
    CHECK (reads ("SAT +6 dB"));
    CHECK (host.settleUntil ([&] { return ! readout.isNotApplied(); }));
    host.edit (parameters::driveId, 13.6f);
    CHECK (reads ("SAT +14 dB"));
    host.edit (parameters::driveId, 0.0f);
    CHECK (reads ("SAT 0 dB"));

    // Off, it still reads Drive, dimmed; on again, it isn't.
    host.edit (parameters::driveId, 9.0f);
    host.edit (parameters::saturationId, 0.0f);
    CHECK (reads ("SAT +9 dB"));
    CHECK (host.settleUntil ([&] { return readout.isNotApplied(); }));
    host.edit (parameters::saturationId, 1.0f);
    CHECK (host.settleUntil ([&] { return ! readout.isNotApplied(); }));
}

TEST_CASE ("The Saturation readout sits just left of the Output readout, and Tab reaches it after the High Quality readout and before Output, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    showView (host, compact);
    auto& saturation = host.saturationReadout();
    auto& output = host.outputReadout();
    const auto sat = host.inEditor (saturation), out = host.inEditor (output);
    CHECK (sat.getRight() + comp1::Footer::chipGap == out.getX());
    CHECK (sat.getCentreY() == out.getCentreY());
    // Wide enough to read its whole text.
    CHECK_FALSE (saturation.isShrunk());
    auto& highQuality = host.highQualityReadout();
    CHECK (sat.getX() > host.inEditor (highQuality).getRight());

    highQuality.grabKeyboardFocus();
    REQUIRE (highQuality.hasKeyboardFocus (false));
    REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
    CHECK (focused() == &saturation);
    REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
    CHECK (focused() == &output);
}

TEST_CASE ("The Saturation readout opens the Saturation popover above it by a click, and by Space or Return, in both views; Tab reaches its controls, and Escape closes it, focus back on the readout")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    showView (host, compact);
    auto& readout = host.saturationReadout();
    auto& popover = host.saturationPopover();
    REQUIRE_FALSE (popover.isOpen());
    REQUIRE_FALSE (popover.isShowing());

    // By a click: above the readout, right-aligned to it, holding its three controls inside the window.
    host.openSaturationPopover();
    CHECK (readout.isOpen());
    const auto card = host.editor->getLocalArea (&popover, popover.getCardBounds());
    const auto anchor = host.inEditor (readout);
    CHECK (card.getBottom() < anchor.getY());
    CHECK (card.getRight() == anchor.getRight());
    for (const auto& title : popoverControls)
    {
        CAPTURE (title);
        const auto inPopover = host.findAll<juce::Component> ([&] (juce::Component& c) { return c.getTitle() == title && c.isShowing() && popover.isParentOf (&c); });
        REQUIRE (inPopover.size() == 1);
        CHECK (host.editor->getLocalBounds().contains (host.inEditor (*inPopover.front())));
    }
    // A second click closes it.
    readout.onClick();
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());

    // By the keys, from the readout: focus moves to the on switch, and Tab walks its controls in order.
    const auto key = GENERATE (juce::KeyPress (juce::KeyPress::spaceKey), juce::KeyPress (juce::KeyPress::returnKey));
    readout.grabKeyboardFocus();
    REQUIRE (readout.hasKeyboardFocus (false));
    CHECK (host.press (key));
    REQUIRE (popover.isOpen());
    REQUIRE (focused() != nullptr);
    CHECK (popover.isParentOf (focused()));
    juce::StringArray walked { focused()->getTitle() };
    for (int i = 1; i < popoverControls.size(); ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        REQUIRE (focused() != nullptr);
        CHECK (popover.isParentOf (focused()));
        walked.add (focused()->getTitle());
    }
    CHECK (walked == popoverControls);
    CHECK (host.press (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());
    CHECK (readout.hasKeyboardFocus (false));
}

TEST_CASE ("The Saturation popover's on switch, Drive and Saturation Position drive their parameters, one undo step each, and follow them, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    showView (host, compact);
    auto& popover = host.openSaturationPopover();
    auto& history = host.processor.session().editHistory();
    const auto in = [&popover] (auto& c) { return popover.isParentOf (&c); };

    auto& on = host.showing<juce::ToggleButton> ("Saturation");
    REQUIRE (in (on));
    host.edit (parameters::saturationId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return ! on.getToggleState(); }));
    int steps = history.undoSteps();
    host::EditorHost::click (on);
    CHECK (host.value (parameters::saturationId) == 1.0f);
    CHECK (history.undoSteps() == steps + 1);

    auto& drive = host.knob ("Drive");
    REQUIRE (in (drive));
    steps = history.undoSteps();
    REQUIRE (drive.commitTypedText ("12"));
    CHECK_THAT (host.value (parameters::driveId), Catch::Matchers::WithinAbs (12.0f, 1.0e-4));
    CHECK (history.undoSteps() == steps + 1);
    host.edit (parameters::driveId, 3.0f);
    CHECK (host.settleUntil ([&] { return juce::approximatelyEqual (drive.getValue(), 3.0); }));

    auto& position = host.showing<juce::ComboBox> ("Saturation Position");
    REQUIRE (in (position));
    host.edit (parameters::saturationPositionId, 0.0f);
    host.settle();
    steps = history.undoSteps();
    host::EditorHost::pick (position, position.getItemText (1));
    CHECK (host.value (parameters::saturationPositionId) == 1.0f);
    CHECK (history.undoSteps() == steps + 1);
    host.edit (parameters::saturationPositionId, 0.0f);
    CHECK (host.settleUntil ([&] { return position.getSelectedItemIndex() == 0; }));
}

TEST_CASE ("The Expanded View's Character section is titled Character and holds Colour alone: Saturation's controls are in the Saturation popover")
{
    host::EditorHost host;
    const auto named = [&host] (const juce::String& name) {
        return host.findAll<comp1::Section> ([&name] (comp1::Section& s) { return s.getName() == name && s.isShowing(); });
    };
    CHECK (named ("Character and Saturation").empty());
    REQUIRE (named ("Character").size() == 1);
    auto& character = *named ("Character").front();
    const auto controls = host.findAll<juce::Component> ([&character] (juce::Component& c) {
        return character.isParentOf (&c) && c.isShowing() && c.getWantsKeyboardFocus();
    });
    juce::StringArray titles;
    for (auto* c : controls)
        titles.add (c->getTitle());
    CHECK (titles == juce::StringArray { "Colour" });
    // Nothing showing outside the footer and the popover (closed) is Saturation's.
    for (const auto* title : { "Saturation Position", "Drive" })
    {
        CAPTURE (title);
        CHECK (host.findAll<juce::Component> ([title] (juce::Component& c) { return c.getTitle() == title && c.isShowing(); }).empty());
    }
}
