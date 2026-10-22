// The footer's Output readout, in both views: what it reads, Output Gain plus Auto Gain's estimate while Auto Gain is
// in effect, and how it is drawn under Global Bypass; and the output popover it opens: opening and closing by mouse and
// keys, Tab and Escape, its layout, its controls' edits and undo steps, and what they follow while it is open.

#include "EditorHost.h"
#include "comp1/Style.h"
#include "plugkit/ui/OutputPopoverFrame.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

namespace parameters = comp1::parameters;

namespace
{

const juce::String minus = juce::String::charToString (0x2212);

// The compressor settings dyn-lab's Auto Gain check reads beyond Threshold at their defaults (tools/dyn-lab/USAGE.md,
// "autogain"), on Clean, with Comp Follow and Auto Gain off.
void dynLabDefaults (host::EditorHost& host)
{
    REQUIRE (host.processor.chooseStyle (comp1::Style::Clean));
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::ratioId, 4.0f);
    host.edit (parameters::kneeId, 6.0f);
    host.edit (parameters::attackId, 10.0f);
    host.edit (parameters::releaseId, 100.0f);
    host.edit (parameters::autoReleaseId, 0.0f);
    host.edit (parameters::holdId, 0.0f);
    host.edit (parameters::rangeId, 60.0f);
}

} // namespace

TEST_CASE ("The Output readout reads Output Gain, plus Auto Gain's estimate while Auto Gain is on or Comp Follow holds it on, signed to one decimal, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& readout = host.outputReadout();
    REQUIRE (readout.isShowing());
    CHECK (readout.getTitle() == "Output");
    const auto reads = [&] (const juce::String& text) { return host.settleUntil ([&] { return readout.getButtonText() == text; }); };

    // Output Gain alone, with Auto Gain off.
    dynLabDefaults (host);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::outputGainId, 3.5f);
    CHECK (reads ("+3.5 dB"));
    host.edit (parameters::outputGainId, -1.8f);
    CHECK (reads (minus + "1.8 dB"));
    // Its bottom, -36 dB: comp1's Output Gain has no silent bottom, so it reads as a level, never -inf dB.
    host.edit (parameters::outputGainId, -36.0f);
    CHECK (reads (minus + "36.0 dB"));

    // Auto Gain on, at Threshold -30 dB on Clean: dyn-lab's estimate, 7.9161 dB (`python3 tools/dyn-lab/dynlab.py
    // autogain --threshold -30 --character clean`), plus Output Gain's +1 dB.
    host.edit (parameters::outputGainId, 1.0f);
    host.edit (parameters::autoGainId, 1.0f);
    CHECK (reads ("+8.9 dB"));

    // Comp Follow holding Auto Gain on, Auto Gain itself off: VCA at Comp 50%, the estimate dyn-lab's compcurve prints
    // for it, 3.5473 dB (`python3 tools/dyn-lab/dynlab.py compcurve --style vca`), plus Output Gain's -2 dB.
    REQUIRE (host.processor.chooseStyle (comp1::Style::VCA));
    host.edit (parameters::compId, 50.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::outputGainId, -2.0f);
    REQUIRE (host.value (parameters::compFollowId) == 1.0f);
    CHECK (reads ("+1.5 dB"));
    // Comp Follow off, Auto Gain off: Output Gain alone again.
    host.edit (parameters::compFollowId, 0.0f);
    CHECK (reads (minus + "2.0 dB"));
}

TEST_CASE ("Under Global Bypass the Output readout is drawn not applied, in text4, and applied again once it is off")
{
    host::EditorHost host;
    auto& readout = host.outputReadout();
    CHECK_FALSE (readout.isNotApplied());
    host.edit (parameters::globalBypassId, 1.0f);
    CHECK (host.settleUntil ([&] { return readout.isNotApplied(); }));
    host.edit (parameters::globalBypassId, 0.0f);
    CHECK (host.settleUntil ([&] { return ! readout.isNotApplied(); }));
}

namespace
{

// The output popover's controls, in Tab's order: each titled with its host parameter's name.
const juce::StringArray popoverControls { "Output Gain", "Ceiling", "Auto Gain", "Limiter" };

juce::Component* focused() { return juce::Component::getCurrentlyFocusedComponent(); }

} // namespace

TEST_CASE ("The Output readout opens the output popover above it by a click, and by Space or Return, in both views; Tab reaches its controls, and Escape closes it, focus back on the readout")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& readout = host.outputReadout();
    auto& popover = host.outputPopover();
    REQUIRE_FALSE (popover.isOpen());
    REQUIRE_FALSE (popover.isShowing());

    // By a click: above the readout, right-aligned to it, holding its four controls inside the window.
    host.openOutputPopover();
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

    // By the keys, from the readout: focus moves to Output Gain, and Tab walks its controls in order.
    const auto key = GENERATE (juce::KeyPress (juce::KeyPress::spaceKey), juce::KeyPress (juce::KeyPress::returnKey));
    readout.grabKeyboardFocus();
    REQUIRE (readout.hasKeyboardFocus (false));
    CHECK (host.press (key));
    REQUIRE (popover.isOpen());
    REQUIRE (focused() != nullptr);
    CHECK (focused()->getTitle() == "Output Gain");
    juce::StringArray walked { focused()->getTitle() };
    for (int i = 1; i < popoverControls.size(); ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        REQUIRE (focused() != nullptr);
        CHECK (popover.isParentOf (focused()));
        CHECK (focused()->hasFocusOutline());
        walked.add (focused()->getTitle());
    }
    CHECK (walked == popoverControls);
    // Escape closes it, focus back on the readout.
    CHECK (host.press (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (popover.isOpen());
    CHECK_FALSE (readout.isOpen());
    CHECK (readout.hasKeyboardFocus (false));
}

TEST_CASE ("The output popover holds Output Gain and Ceiling side by side, and under them one row of letter toggles, A for Auto Gain and L for the Limiter, no checkbox among them, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    host.edit (parameters::compFollowId, 0.0f);
    auto& popover = host.openOutputPopover();
    const auto inPopover = [&popover] (juce::Component& c) { return popover.getLocalArea (&c, c.getLocalBounds()); };

    // The knobs' faces on one line, Output Gain on the left.
    auto& outputGain = host.knob ("Output Gain");
    auto& ceiling = host.knob ("Ceiling");
    const auto gainFace = popover.getLocalPoint (&outputGain, outputGain.getFaceCentre());
    const auto ceilingFace = popover.getLocalPoint (&ceiling, ceiling.getFaceCentre());
    CHECK (juce::exactlyEqual (gainFace.y, ceilingFace.y));
    CHECK (gainFace.x < ceilingFace.x);

    // A and L: letters, 30 px tall, the same width side by side, A on the left, under the knobs' labels, in the card.
    auto& autoGain = host.showing<plugkit::ui::OutputToggle> ("Auto Gain");
    auto& limiter = host.showing<plugkit::ui::OutputToggle> ("Limiter");
    CHECK (autoGain.getButtonText() == "A");
    CHECK (limiter.getButtonText() == "L");
    const auto a = inPopover (autoGain), l = inPopover (limiter);
    CHECK (a.getHeight() == 30);
    CHECK (l.getHeight() == 30);
    CHECK (a.getY() == l.getY());
    CHECK (a.getWidth() == l.getWidth());
    CHECK (a.getRight() < l.getX());
    const auto labels = host.findAll<juce::Label> ([&popover] (juce::Label& each) { return popover.isParentOf (&each) && each.isShowing(); });
    CHECK (labels.size() == 2);
    for (auto* label : labels)
        CHECK (inPopover (*label).getBottom() <= a.getY());
    CHECK (popover.getCardBounds().contains (a.getUnion (l)));

    // Their tooltips say what each does.
    CHECK (autoGain.getTooltip().startsWith ("Auto Gain: "));
    CHECK (limiter.getTooltip().startsWith ("Limiter: "));
    CHECK (limiter.getTooltip().contains ("Ceiling"));

    // No checkbox left.
    CHECK (host.findAll<juce::ToggleButton> ([&popover] (juce::ToggleButton& each) { return popover.isParentOf (&each); }).empty());
}

TEST_CASE ("The output popover's Ceiling stays enabled while the Limiter is off, as it still applies at delivery")
{
    host::EditorHost host;
    host.edit (parameters::limiterId, 0.0f);
    host.openOutputPopover();
    host.settle();
    CHECK_FALSE (host.showing<plugkit::ui::OutputToggle> ("Limiter").getToggleState());
    CHECK (host.knob ("Ceiling").isEnabled());
}

TEST_CASE ("The output popover's controls set their host parameters, each edit one undo step")
{
    host::EditorHost host;
    host.edit (parameters::compFollowId, 0.0f);
    host.openOutputPopover();
    auto& history = host.processor.session().editHistory();
    REQUIRE (host.settleUntil ([&] { return host.showing<plugkit::ui::OutputToggle> ("Auto Gain").isEnabled(); }));

    int steps = history.undoSteps();
    host::EditorHost::drag (host.knob ("Output Gain"), 20.0f);
    CHECK (host.value (parameters::outputGainId) > 0.0f);
    CHECK (history.undoSteps() == ++steps);
    host::EditorHost::drag (host.knob ("Ceiling"), -20.0f);
    CHECK (host.value (parameters::ceilingId) < 0.0f);
    CHECK (history.undoSteps() == ++steps);
    for (const juce::String toggle : { "Auto Gain", "Limiter" })
    {
        CAPTURE (toggle);
        const auto& id = toggle == "Limiter" ? parameters::limiterId : parameters::autoGainId;
        REQUIRE (host.value (id) == 0.0f);
        host::EditorHost::click (host.showing<plugkit::ui::OutputToggle> (toggle));
        CHECK (host.value (id) == 1.0f);
        CHECK (history.undoSteps() == ++steps);
    }
    // Undone, each back as it was.
    for (int i = 0; i < 4; ++i)
        history.undo();
    CHECK (host.value (parameters::outputGainId) == 0.0f);
    CHECK (host.value (parameters::ceilingId) == 0.0f);
    CHECK (host.value (parameters::autoGainId) == 0.0f);
    CHECK (host.value (parameters::limiterId) == 0.0f);
}

TEST_CASE ("While open, the output popover's controls follow Host Automation, undo, A/B Compare and Presets")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    host.edit (parameters::compFollowId, 0.0f);
    host.openOutputPopover();
    auto& outputGain = host.knob ("Output Gain");
    auto& ceiling = host.knob ("Ceiling");
    auto& limiter = host.showing<plugkit::ui::OutputToggle> ("Limiter");
    auto& autoGain = host.showing<plugkit::ui::OutputToggle> ("Auto Gain");
    const auto shows = [&] (double gain, double ceilingDb, bool limiterOn, bool autoGainOn) {
        return host.settleUntil ([&] {
            return std::abs (outputGain.getValue() - gain) < 1.0e-3 && std::abs (ceiling.getValue() - ceilingDb) < 1.0e-3
                   && limiter.getToggleState() == limiterOn && autoGain.getToggleState() == autoGainOn;
        });
    };

    // Host Automation.
    host.set (parameters::outputGainId, 6.0f);
    host.set (parameters::ceilingId, -3.0f);
    host.set (parameters::limiterId, 1.0f);
    host.set (parameters::autoGainId, 1.0f);
    CHECK (shows (6.0, -3.0, true, true));

    // Undo of an edit.
    host.edit (parameters::outputGainId, -4.0f);
    CHECK (shows (-4.0, -3.0, true, true));
    session.editHistory().undo();
    CHECK (shows (6.0, -3.0, true, true));

    // A/B Compare: B, a copy of A, edited, then back to A.
    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    host.edit (parameters::outputGainId, 2.0f);
    host.edit (parameters::limiterId, 0.0f);
    CHECK (shows (2.0, -3.0, false, true));
    session.selectCompareSide (plugkit::ui::CompareSide::A);
    CHECK (shows (6.0, -3.0, true, true));

    // A Preset, holding other settings, loaded.
    host.edit (parameters::outputGainId, -1.0f);
    host.edit (parameters::ceilingId, -6.0f);
    host.edit (parameters::limiterId, 0.0f);
    host.edit (parameters::autoGainId, 0.0f);
    const auto preset = session.presetState();
    host.edit (parameters::outputGainId, 6.0f);
    host.edit (parameters::ceilingId, -3.0f);
    host.edit (parameters::limiterId, 1.0f);
    host.edit (parameters::autoGainId, 1.0f);
    CHECK (shows (6.0, -3.0, true, true));
    REQUIRE (session.loadPreset (preset, "Quiet"));
    CHECK (shows (-1.0, -6.0, false, false));
    CHECK (host.outputPopover().isOpen());
}
