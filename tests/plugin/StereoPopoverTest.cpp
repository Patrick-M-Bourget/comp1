// The footer's Stereo readout, in both views: what it reads, Stereo Placement and Stereo Link, and how it is drawn on a
// mono track; the stereo popover it opens: opening and closing by mouse and keys, Tab and Escape, and its controls'
// edits; and the Expanded View's body, which has no Stereo section.

#include "EditorHost.h"
#include "StereoPopover.h"
#include "plugkit/ui/FooterReadout.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace parameters = comp1::parameters;
using Catch::Matchers::WithinAbs;

namespace
{

// The stereo popover's controls, in Tab's order: each titled with its host parameter's name.
const juce::StringArray popoverControls { "Stereo Placement", "Stereo Link" };

} // namespace

TEST_CASE ("The Stereo readout reads Stereo Placement then Stereo Link in whole percent, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    host.showView (compact);
    auto& readout = host.stereoReadout();
    REQUIRE (readout.isShowing());
    CHECK (readout.getTitle() == "Stereo");
    CHECK (host.footer().isParentOf (&readout));
    const auto reads = [&] (const juce::String& text) { return host.settleUntil ([&] { return readout.getButtonText() == text; }); };

    // The defaults: Stereo, fully linked.
    CHECK (reads ("Stereo 100%"));
    // Each Placement, by its choice's index (Stereo, Mid, Side), with Link rounded to a whole percent.
    host.edit (parameters::stereoLinkId, 80.0f);
    host.edit (parameters::stereoPlacementId, 1.0f);
    CHECK (reads ("Mid 80%"));
    host.edit (parameters::stereoLinkId, 0.0f);
    host.edit (parameters::stereoPlacementId, 2.0f);
    CHECK (reads ("Side 0%"));
    host.edit (parameters::stereoLinkId, 42.6f);
    host.edit (parameters::stereoPlacementId, 0.0f);
    CHECK (reads ("Stereo 43%"));
}

TEST_CASE ("Given too little room for its text, the Stereo readout shrinks to its label, ST, still showing and overlapping neither the Processing Mode readout nor the Output readout")
{
    host::EditorHost host;
    auto& footer = host.footer();
    auto& item = footer.stereo();
    auto& readout = host.stereoReadout();
    auto& highQuality = footer.processingMode();
    REQUIRE_FALSE (readout.isShrunk());
    // Narrower a pixel at a time, until the row between the chips and the Output readout can't hold its text.
    while (! readout.isShrunk() && footer.getWidth() > 0)
        footer.setSize (footer.getWidth() - 1, footer.getHeight());
    REQUIRE (readout.isShrunk());
    CHECK (readout.isShowing());
    CHECK (readout.getWidth() == readout.getShrunkWidth());
    CHECK (item.getX() >= highQuality.getRight() + comp1::Footer::chipGap);
    CHECK (item.getRight() + comp1::Footer::chipGap <= footer.output().getX());
    // Read whole by a screen reader all the same.
    CHECK (readout.getButtonText() == "Stereo 100%");
}

TEST_CASE ("On a mono track the Stereo readout is drawn not applied, its description saying why, and the stereo popover's controls are disabled, their values kept; on stereo they are applied and enabled, with no description")
{
    host::EditorHost host;
    host.edit (parameters::stereoPlacementId, 1.0f);
    host.edit (parameters::stereoLinkId, 40.0f);
    const auto channels = GENERATE (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
    CAPTURE (channels.getDescription());
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
    const bool stereo = channels == juce::AudioChannelSet::stereo();
    auto& readout = host.stereoReadout();
    REQUIRE (host.settleUntil ([&] { return readout.isNotApplied() == ! stereo; }));
    CHECK (readout.getButtonText() == "Mid 40%");
    // What a screen reader is told beside its value.
    auto* handler = readout.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getDescription() == (stereo ? juce::String() : juce::String ("Mono track: Stereo Placement and Stereo Link change nothing")));

    host.openStereoPopover();
    auto& placement = host.showing<juce::ComboBox> ("Stereo Placement");
    auto& link = host.knob ("Stereo Link");
    REQUIRE (host.settleUntil ([&] { return placement.isEnabled() == stereo && link.isEnabled() == stereo; }));
    CHECK (placement.getSelectedItemIndex() == 1);
    CHECK_THAT (link.getValue(), WithinAbs (40.0, 1.0e-4));
    CHECK (host.value (parameters::stereoPlacementId) == 1.0f);
    CHECK_THAT (host.value (parameters::stereoLinkId), WithinAbs (40.0f, 1.0e-4));
}

TEST_CASE ("The Stereo readout opens the stereo popover above it by a click, and by Space or Return, in both views; Tab reaches its controls, and Escape closes it, focus back on the readout")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    host.showView (compact);
    auto& readout = host.stereoReadout();
    auto& popover = host.stereoPopover();
    REQUIRE_FALSE (popover.isOpen());
    REQUIRE_FALSE (popover.isShowing());

    // By a click: above the readout, right-aligned to it, holding its two controls inside the window.
    host.openStereoPopover();
    CHECK (readout.isOpen());
    const auto card = host.editor->getLocalArea (&popover, popover.getCardBounds());
    const auto anchor = host.inEditor (readout);
    CHECK (card.getBottom() < anchor.getY());
    CHECK (card.getRight() == anchor.getRight());
    for (const auto& title : popoverControls)
    {
        CAPTURE (title);
        auto& control = host.showing<juce::Component> (title);
        CHECK (popover.isParentOf (&control));
        CHECK (host.editor->getLocalBounds().contains (host.inEditor (control)));
    }
    // A second click closes it.
    readout.onClick();
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());

    // By the keys, from the readout: focus moves to Stereo Placement, and Tab reaches Stereo Link.
    const auto key = GENERATE (juce::KeyPress (juce::KeyPress::spaceKey), juce::KeyPress (juce::KeyPress::returnKey));
    readout.grabKeyboardFocus();
    REQUIRE (readout.hasKeyboardFocus (false));
    CHECK (host.press (key));
    REQUIRE (popover.isOpen());
    REQUIRE (host.focused() != nullptr);
    juce::StringArray walked { host.focused()->getTitle() };
    for (int i = 1; i < popoverControls.size(); ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        REQUIRE (host.focused() != nullptr);
        CHECK (popover.isParentOf (host.focused()));
        walked.add (host.focused()->getTitle());
    }
    CHECK (walked == popoverControls);
    // Escape closes it, focus back on the readout.
    CHECK (host.press (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());
    CHECK (readout.hasKeyboardFocus (false));
}

TEST_CASE ("The stereo popover's controls set Stereo Placement and Stereo Link, each edit one undo step, and the readout follows")
{
    host::EditorHost host;
    host.openStereoPopover();
    auto& history = host.processor.session().editHistory();
    auto& placement = host.showing<juce::ComboBox> ("Stereo Placement");
    auto& link = host.knob ("Stereo Link");
    REQUIRE (host.settleUntil ([&] { return placement.isEnabled() && link.isEnabled(); }));

    int steps = history.undoSteps();
    placement.setSelectedItemIndex (2, juce::sendNotificationSync);
    CHECK (host.value (parameters::stereoPlacementId) == 2.0f);
    CHECK (history.undoSteps() == ++steps);
    host::EditorHost::drag (link, -20.0f);
    const float linked = host.value (parameters::stereoLinkId);
    CHECK (linked < 100.0f);
    CHECK (history.undoSteps() == ++steps);
    CHECK (host.settleUntil ([&] { return host.stereoReadout().getButtonText() == "Side " + juce::String (juce::roundToInt (linked)) + "%"; }));

    // Undone, each back as it was.
    history.undo();
    history.undo();
    CHECK (host.value (parameters::stereoPlacementId) == 0.0f);
    CHECK (host.value (parameters::stereoLinkId) == 100.0f);
    CHECK (host.settleUntil ([&] { return host.stereoReadout().getButtonText() == "Stereo 100%"; }));
}

TEST_CASE ("While open, the stereo popover's controls follow Host Automation")
{
    host::EditorHost host;
    host.openStereoPopover();
    auto& placement = host.showing<juce::ComboBox> ("Stereo Placement");
    auto& link = host.knob ("Stereo Link");
    host.set (parameters::stereoPlacementId, 1.0f);
    host.set (parameters::stereoLinkId, 25.0f);
    CHECK (host.settleUntil ([&] { return placement.getSelectedItemIndex() == 1 && std::abs (link.getValue() - 25.0) < 1.0e-3; }));
}

TEST_CASE ("The Expanded View's body has no Stereo section, and Stereo Placement and Stereo Link show only in the stereo popover")
{
    host::EditorHost host;
    auto* sections = harness::findChild<juce::Component> (*host.editor, [] (juce::Component& c) { return c.getName() == "Sections"; });
    REQUIRE (sections != nullptr);
    CHECK (harness::findChild<juce::Component> (*sections,[] (juce::Component& c) { return c.getName() == "Stereo"; }) == nullptr);
    for (const auto& title : popoverControls)
    {
        CAPTURE (title);
        CHECK (host.findAll<juce::Component> ([&title] (juce::Component& c) { return c.getTitle() == title && c.isShowing(); }).empty());
    }
}
