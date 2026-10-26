// The footer's High Quality readout, in both views: what it reads, the Oversampling in force, and how it is drawn in
// Zero Latency; and the High Quality popover it opens, holding Lookahead and Oversampling: opening and closing by mouse
// and keys, its controls' edits, undo steps and availability, and a pick from Oversampling's menu leaving it open. The
// Expanded View's body has no Lookahead of its own.

#include "EditorHost.h"
#include "Measure.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
namespace parameters = comp1::parameters;

namespace
{

// The choices' texts the Oversampling chip's menu offers.
juce::StringArray offered (comp1::ChoiceChip& chip)
{
    juce::StringArray items;
    const auto menu = chip.menu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        items.add (it.getItem().text);
    return items;
}

} // namespace

TEST_CASE ("The High Quality readout reads HQ and the Oversampling in force, in both views, and shrinks to HQ")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    host.edit (parameters::processingModeId, 1.0f);
    auto& readout = host.highQualityReadout();
    REQUIRE (readout.isShowing());
    CHECK (readout.getTitle() == "High Quality");
    const auto reads = [&] (const juce::String& text) { return host.settleUntil ([&] { return readout.getButtonText() == text; }); };

    host.edit (parameters::oversamplingId, 1.0f);
    CHECK (reads ("HQ 2x"));
    host.edit (parameters::oversamplingId, 3.0f);
    CHECK (reads ("HQ 8x"));
    // 8x stored at 192 kHz: 2x, the cap, in force.
    host.processor.prepareToPlay (192000.0, 512);
    CHECK (reads ("HQ 2x"));
    // Given only its label's width, it shows its label.
    CHECK (readout.getShrunkWidth() < readout.getIdealWidth());
}

TEST_CASE ("In Zero Latency the High Quality readout is drawn not applied, in text4, and applied again in High Quality")
{
    host::EditorHost host;
    auto& readout = host.highQualityReadout();
    host.edit (parameters::processingModeId, 0.0f);
    CHECK (host.settleUntil ([&] { return readout.isNotApplied(); }));
    host.edit (parameters::processingModeId, 1.0f);
    CHECK (host.settleUntil ([&] { return ! readout.isNotApplied(); }));
}

TEST_CASE ("The High Quality readout opens the High Quality popover above it by a click, and by Space or Return, in both views; Tab walks Lookahead then Oversampling, and Escape closes it, focus back on the readout")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    host.edit (parameters::processingModeId, 1.0f);
    auto& readout = host.highQualityReadout();
    auto& popover = host.highQualityPopover();
    REQUIRE_FALSE (popover.isOpen());
    REQUIRE_FALSE (popover.isShowing());

    // By a click: above the readout, right-aligned to it, holding Lookahead and Oversampling inside the window.
    host.openHighQualityPopover();
    CHECK (readout.isOpen());
    const auto card = host.editor->getLocalArea (&popover, popover.getCardBounds());
    const auto anchor = host.inEditor (readout);
    CHECK (card.getBottom() < anchor.getY());
    CHECK (card.getRight() == anchor.getRight());
    for (const juce::String title : { "Lookahead", "Oversampling" })
    {
        CAPTURE (title);
        auto& control = host.showing<juce::Component> (title);
        CHECK (popover.isParentOf (&control));
        CHECK (popover.getCardBounds().contains (popover.getLocalArea (&control, control.getLocalBounds())));
        CHECK (host.editor->getLocalBounds().contains (host.inEditor (control)));
    }
    // A second click closes it.
    readout.onClick();
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());

    // By the keys, from the readout: focus moves to Lookahead, and Tab to Oversampling.
    const auto key = GENERATE (juce::KeyPress (juce::KeyPress::spaceKey), juce::KeyPress (juce::KeyPress::returnKey));
    readout.grabKeyboardFocus();
    REQUIRE (readout.hasKeyboardFocus (false));
    CHECK (host.press (key));
    REQUIRE (popover.isOpen());
    REQUIRE (host.focused() != nullptr);
    CHECK (host.focused()->getTitle() == "Lookahead");
    REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
    REQUIRE (host.focused() != nullptr);
    CHECK (host.focused()->getTitle() == "Oversampling");
    CHECK (popover.isParentOf (host.focused()));
    // Escape closes it, focus back on the readout.
    CHECK (host.press (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());
    CHECK (readout.hasKeyboardFocus (false));
}

TEST_CASE ("The High Quality popover's Lookahead and Oversampling set their host parameters, each edit one undo step")
{
    host::EditorHost host;
    host.edit (parameters::processingModeId, 1.0f);
    host.edit (parameters::lookaheadId, 0.0f);
    host.edit (parameters::oversamplingId, 0.0f);
    host.openHighQualityPopover();
    auto& history = host.processor.session().editHistory();
    REQUIRE (host.settleUntil ([&] { return host.knob ("Lookahead").isEnabled(); }));

    int steps = history.undoSteps();
    host::EditorHost::drag (host.knob ("Lookahead"), 40.0f);
    CHECK (host.value (parameters::lookaheadId) > 0.0f);
    CHECK (history.undoSteps() == ++steps);

    auto& chip = host.showing<comp1::ChoiceChip> ("Oversampling");
    auto action = harness::actionOf (chip.menu(), "4x");
    REQUIRE (action != nullptr);
    action();
    CHECK (host.value (parameters::oversamplingId) == 2.0f);
    CHECK (history.undoSteps() == ++steps);
    CHECK (host.settleUntil ([&] { return chip.getButtonText() == "Oversampling: 4x" && host.highQualityReadout().getButtonText() == "HQ 4x"; }));

    history.undo();
    history.undo();
    CHECK (host.value (parameters::oversamplingId) == 0.0f);
    CHECK (host.value (parameters::lookaheadId) == 0.0f);
}

TEST_CASE ("In Zero Latency the High Quality popover's Lookahead and Oversampling are disabled, not hidden, their values kept; in High Quality they are enabled")
{
    host::EditorHost host;
    host.edit (parameters::lookaheadId, 2.0f);
    host.openHighQualityPopover();
    auto& lookahead = host.knob ("Lookahead");
    auto& oversampling = host.showing<comp1::ChoiceChip> ("Oversampling");
    const auto enabled = [&] (bool on) { return lookahead.isEnabled() == on && oversampling.isEnabled() == on; };
    REQUIRE (host.settleUntil ([&] { return enabled (false); }));
    CHECK (lookahead.isShowing());
    CHECK (oversampling.isShowing());
    host.edit (parameters::processingModeId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return enabled (true); }));
    host.edit (parameters::processingModeId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return enabled (false); }));
    CHECK (host.highQualityPopover().isOpen());
    CHECK_THAT (host.value (parameters::lookaheadId), WithinAbs (2.0f, 1.0e-4));
}

TEST_CASE ("The popover's Oversampling offers the factors up to each sample rate's cap and shows the one in force; before a sample rate, the assumed one's")
{
    host::EditorHost host;
    host.edit (parameters::processingModeId, 1.0f);
    host.openHighQualityPopover();
    auto& chip = host.showing<comp1::ChoiceChip> ("Oversampling");
    const juce::StringArray all { "1x", "2x", "4x", "8x" };
    // Not prepared yet: 48 kHz assumed.
    CHECK (offered (chip) == all);

    const double sampleRate = GENERATE (from_range (comp1::test::sampleRates()));
    CAPTURE (sampleRate);
    host.processor.prepareToPlay (sampleRate, 512);
    // 1x to 8x at 44.1 and 48 kHz, to 4x at 88.2 and 96 kHz, to 2x at 176.4 and 192 kHz.
    const int factors = sampleRate <= 48000.0 ? 4 : sampleRate <= 96000.0 ? 3 : 2;
    juce::StringArray expected;
    for (int i = 0; i < factors; ++i)
        expected.add (all[i]);
    CHECK (offered (chip) == expected);

    // 8x stored: the cap shown, and ticked.
    host.edit (parameters::oversamplingId, 3.0f);
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == "Oversampling: " + expected[factors - 1]; }));
    const auto menu = chip.menu();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        CHECK (it.getItem().isTicked == (it.getItem().text == expected[factors - 1]));
    // Picking one writes it.
    auto action = harness::actionOf (menu, "1x");
    REQUIRE (action != nullptr);
    action();
    CHECK (host.value (parameters::oversamplingId) == 0.0f);
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == "Oversampling: 1x"; }));
}

TEST_CASE ("A pick from the Oversampling chip's menu, a window of its own, leaves the High Quality popover open; a press elsewhere in the editor closes it")
{
    host::EditorHost host;
    host.edit (parameters::processingModeId, 1.0f);
    host.edit (parameters::oversamplingId, 0.0f);
    auto& popover = host.openHighQualityPopover();
    auto& chip = host.showing<comp1::ChoiceChip> ("Oversampling");
    REQUIRE (host.settleUntil ([&] { return chip.getButtonText() == "Oversampling: 1x"; }));
    REQUIRE (chip.onClick != nullptr);
    chip.onClick();
    host.pressInOpenMenu (popover, "2x");
    CHECK (popover.isOpen());
    // The pick: JUCE posts a menu item's action to the message loop, which tests don't run, so it runs here.
    auto action = harness::actionOf (chip.menu(), "2x");
    REQUIRE (action != nullptr);
    action();
    juce::PopupMenu::dismissAllActiveMenus();
    REQUIRE (host.settleUntil ([] { return harness::openMenu() == nullptr; }));
    CHECK (host.value (parameters::oversamplingId) == 1.0f);
    CHECK (popover.isOpen());

    auto& display = host.expandedView().display();
    popover.mouseDown (harness::mouseEvent (display, { 4.0f, 4.0f }));
    CHECK_FALSE (popover.isOpen());
}

TEST_CASE ("The Expanded View's body has no Lookahead: its only Lookahead control is the High Quality popover's")
{
    host::EditorHost host;
    host.edit (parameters::processingModeId, 1.0f);
    auto* sections = harness::findChild<juce::Component> (*host.editor, [] (juce::Component& c) { return c.getName() == "Sections"; });
    REQUIRE (sections != nullptr);
    for (auto* section : sections->getChildren())
        CHECK (section->getName() != "Lookahead");
    CHECK (host.findAll<staple::Knob> ([] (staple::Knob& k) { return k.getTitle() == "Lookahead" && k.isShowing(); }).empty());
    host.openHighQualityPopover();
    const auto knobs = host.findAll<staple::Knob> ([] (staple::Knob& k) { return k.getTitle() == "Lookahead" && k.isShowing(); });
    REQUIRE (knobs.size() == 1);
    CHECK (host.highQualityPopover().isParentOf (knobs.front()));
}
