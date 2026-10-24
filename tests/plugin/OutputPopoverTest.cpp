// The footer's Output readout, in both views: what it reads, Output Gain plus Auto Gain's estimate while Auto Gain is
// in effect, and how it is drawn under Global Bypass; the Auto Gain dot beside it; and the output popover it opens: opening and closing by mouse and
// keys, Tab and Escape, its layout, its controls' edits and undo steps, and what they follow while it is open.

#include "EditorHost.h"
#include "comp1/Style.h"
#include "plugkit/ui/OutputPopoverFrame.h"
#include "plugkit/ui/PopoverSlider.h"
#include "plugkit/ui/ValueText.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <tuple>
#include <vector>

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

TEST_CASE ("The Output readout reads Output Gain, plus Auto Gain's estimate while Auto Gain is on, with Comp Follow on or off, signed to one decimal, in both views")
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

    // Comp Follow on with Auto Gain on: VCA at Comp 50%, the estimate dyn-lab's compcurve prints for it, 3.5473 dB
    // (`python3 tools/dyn-lab/dynlab.py compcurve --style vca`), plus Output Gain's -2 dB.
    REQUIRE (host.processor.chooseStyle (comp1::Style::VCA));
    host.edit (parameters::compId, 50.0f);
    host.edit (parameters::outputGainId, -2.0f);
    REQUIRE (host.value (parameters::compFollowId) == 1.0f);
    REQUIRE (host.value (parameters::autoGainId) == 1.0f);
    CHECK (reads ("+1.5 dB"));
    // Comp Follow on, Auto Gain off: Output Gain alone, as Comp Follow doesn't hold Auto Gain on.
    host.edit (parameters::autoGainId, 0.0f);
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
const juce::StringArray popoverControls { "Output Gain", "Ceiling", "Dry", "Wet", "Auto Gain", "Limiter" };

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

    // By a click: above the readout, right-aligned to it, holding its six controls inside the window.
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

namespace
{

constexpr float off = -std::numeric_limits<float>::infinity();
const juce::String parallelMark = "||";

// A press on slider's track at x, a drag by dx and the release; clicks 2 for a double-click's second press.
void press (plugkit::ui::PopoverSlider& slider, float x, float dx = 0.0f, int clicks = 1)
{
    const juce::Point<float> from { x, 7.0f }, to { x + dx, 7.0f };
    const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
    slider.mouseDown (harness::mouseEvent (slider, from, left, from, clicks));
    if (dx != 0.0f)
        slider.mouseDrag (harness::mouseEvent (slider, to, left, from, clicks));
    slider.mouseUp (harness::mouseEvent (slider, to, {}, from, clicks));
}

} // namespace

TEST_CASE ("The output popover holds Dry then Wet as full-width level sliders between the knobs and the A/L row, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& popover = host.openOutputPopover();
    const auto inPopover = [&popover] (juce::Component& c) { return popover.getLocalArea (&c, c.getLocalBounds()); };

    auto& dry = host.showing<plugkit::ui::PopoverSlider> ("Dry");
    auto& wet = host.showing<plugkit::ui::PopoverSlider> ("Wet");
    for (auto* slider : { &dry, &wet })
    {
        CAPTURE (slider->getTitle());
        CHECK (slider->getFill() == plugkit::ui::PopoverSlider::Fill::fromLeft);
        CHECK (slider->getHeight() == plugkit::ui::PopoverSlider::height);
        CHECK (slider->getTooltip().startsWith (slider->getTitle() + ": "));
    }
    const auto d = inPopover (dry), w = inPopover (wet);
    const auto a = inPopover (host.showing<plugkit::ui::OutputToggle> ("Auto Gain"));
    const auto l = inPopover (host.showing<plugkit::ui::OutputToggle> ("Limiter"));
    // The A/L row's full width, Dry over Wet over it.
    CHECK (d.getX() == a.getX());
    CHECK (d.getRight() == l.getRight());
    CHECK (w.getX() == d.getX());
    CHECK (w.getWidth() == d.getWidth());
    CHECK (d.getBottom() < w.getY());
    CHECK (w.getBottom() < a.getY());
    // Under the knobs' labels.
    for (auto* label : host.findAll<juce::Label> ([&popover] (juce::Label& each) { return popover.isParentOf (&each) && each.isShowing(); }))
        CHECK (inPopover (*label).getBottom() < d.getY());
    CHECK (popover.getCardBounds().contains (d.getUnion (w)));
}

TEST_CASE ("The Dry and Wet sliders set their levels across the full travel, Off at the left end and +12 dB at the right, each gesture one undo step, a double-click their defaults")
{
    host::EditorHost host;
    host.openOutputPopover();
    auto& history = host.processor.session().editHistory();
    for (const auto& [title, id, defaultDb] : { std::tuple { "Dry", parameters::dryId, off }, std::tuple { "Wet", parameters::wetId, 0.0f } })
    {
        CAPTURE (title);
        auto& slider = host.showing<plugkit::ui::PopoverSlider> (title);
        const float width = static_cast<float> (slider.getWidth());
        int steps = history.undoSteps();

        press (slider, width);
        CHECK (host.value (id) == 12.0f);
        CHECK (history.undoSteps() == ++steps);
        press (slider, 0.0f);
        CHECK (std::isinf (host.value (id)));
        CHECK (host.value (id) < 0.0f);
        CHECK (history.undoSteps() == ++steps);
        // Halfway along the travel, even in dB from -52 dB: -20 dB, the parameter's own mapping. A press and a drag
        // are one gesture.
        press (slider, 0.0f, width / 2.0f);
        CHECK_THAT (host.value (id), Catch::Matchers::WithinAbs (-20.0f, 1.0e-3));
        CHECK (history.undoSteps() == ++steps);
        CHECK (slider.readoutNow() == plugkit::ui::PopoverSlider::Readout { title, {}, minus + "20.0 dB" });

        // A double-click: its second press sets the default, one more undo step.
        press (slider, width / 4.0f);
        press (slider, width / 4.0f, 0.0f, 2);
        CHECK (host.value (id) == defaultDb);
        CHECK (history.undoSteps() == steps + 2);
        history.undo();
        CHECK (host.value (id) != defaultDb);
    }
}

TEST_CASE ("The Dry and Wet sliders read Off at the bottom, else their level signed to 0.1 dB, and a screen reader reads them by title and value")
{
    host::EditorHost host;
    host.openOutputPopover();
    auto& dry = host.showing<plugkit::ui::PopoverSlider> ("Dry");
    auto& wet = host.showing<plugkit::ui::PopoverSlider> ("Wet");
    using Readout = plugkit::ui::PopoverSlider::Readout;
    host.edit (parameters::dryId, off);
    host.edit (parameters::wetId, -6.04f);
    REQUIRE (host.settleUntil ([&] { return dry.readoutNow() == Readout { "Dry", {}, "Off" }; }));
    CHECK (wet.readoutNow() == Readout { "Wet", {}, minus + "6.0 dB" });
    host.edit (parameters::wetId, 3.5f);
    CHECK (host.settleUntil ([&] { return wet.readoutNow() == Readout { "Wet", {}, "+3.5 dB" }; }));
    host.edit (parameters::wetId, 0.0f);
    CHECK (host.settleUntil ([&] { return wet.readoutNow() == Readout { "Wet", {}, "0.0 dB" }; }));

    const auto spoken = [] (plugkit::ui::PopoverSlider& slider) {
        auto* handler = slider.getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        CHECK (handler->getRole() == juce::AccessibilityRole::slider);
        REQUIRE (handler->getValueInterface() != nullptr);
        return handler->getValueInterface()->getCurrentValueAsString();
    };
    CHECK (spoken (dry) == "Off");
    CHECK (spoken (wet).contains ("dB"));
    // What it reads, typed back, sets it.
    REQUIRE (wet.commitTypedText ("-3 dB"));
    CHECK_THAT (host.value (parameters::wetId), Catch::Matchers::WithinAbs (-3.0f, 1.0e-4));
    REQUIRE (dry.commitTypedText ("Off"));
    CHECK (std::isinf (host.value (parameters::dryId)));
}

TEST_CASE ("While open, the Dry and Wet sliders follow Host Automation, undo, A/B Compare and Presets")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    host.openOutputPopover();
    auto& dry = host.showing<plugkit::ui::PopoverSlider> ("Dry");
    auto& wet = host.showing<plugkit::ui::PopoverSlider> ("Wet");
    const auto shows = [&] (const juce::String& dryText, const juce::String& wetText) {
        return host.settleUntil ([&] { return dry.readoutNow().right == dryText && wet.readoutNow().right == wetText; });
    };
    CHECK (shows ("Off", "0.0 dB"));

    host.set (parameters::dryId, -12.0f);
    host.set (parameters::wetId, -3.0f);
    CHECK (shows (minus + "12.0 dB", minus + "3.0 dB"));

    host.edit (parameters::dryId, 6.0f);
    CHECK (shows ("+6.0 dB", minus + "3.0 dB"));
    session.editHistory().undo();
    CHECK (shows (minus + "12.0 dB", minus + "3.0 dB"));

    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    host.edit (parameters::dryId, off);
    CHECK (shows ("Off", minus + "3.0 dB"));
    session.selectCompareSide (plugkit::ui::CompareSide::A);
    CHECK (shows (minus + "12.0 dB", minus + "3.0 dB"));

    host.edit (parameters::dryId, -24.0f);
    host.edit (parameters::wetId, off);
    const auto preset = session.presetState();
    host.edit (parameters::dryId, off);
    host.edit (parameters::wetId, 0.0f);
    CHECK (shows ("Off", "0.0 dB"));
    REQUIRE (session.loadPreset (preset, "Parallel"));
    CHECK (shows (minus + "24.0 dB", "Off"));
}

TEST_CASE ("The Output readout shows the parallel mark exactly while Dry is not Off, and says parallel to a screen reader, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& readout = host.outputReadout();
    host.edit (parameters::compFollowId, 0.0f);
    host.edit (parameters::autoGainId, 0.0f);
    host.edit (parameters::outputGainId, -1.8f);
    host.edit (parameters::dryId, off);
    REQUIRE (host.settleUntil ([&] { return readout.getButtonText() == minus + "1.8 dB"; }));
    CHECK (readout.getDescription().isEmpty());

    // Dry at its lowest level above Off, and up.
    for (const float dry : { -51.9f, 0.0f, 12.0f })
    {
        CAPTURE (dry);
        host.edit (parameters::dryId, dry);
        CHECK (host.settleUntil ([&] { return readout.getButtonText() == parallelMark + " " + minus + "1.8 dB"; }));
        CHECK (readout.getDescription() == "Parallel compression on: Dry is mixed in");
        auto* handler = readout.getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        CHECK (handler->getDescription().contains ("Parallel"));
        // Wide enough for the mark.
        CHECK (readout.getWidth() >= readout.getIdealWidth());
    }
    host.edit (parameters::dryId, off);
    CHECK (host.settleUntil ([&] { return readout.getButtonText() == minus + "1.8 dB"; }));
    CHECK (readout.getDescription().isEmpty());
}

namespace
{

// The A toggle's description, which its tooltip starts with.
const juce::String autoGainDescription = "Auto Gain: compensates the output level, estimated from the settings, so compression is heard without a loudness bias";

// The bounds of footer's children other than the Auto Gain dot, in order.
std::vector<juce::Rectangle<int>> layoutOf (comp1::Footer& footer)
{
    std::vector<juce::Rectangle<int>> bounds;
    for (auto* child : footer.getChildren())
        if (child != &footer.autoGainDot())
            bounds.push_back (child->getBounds());
    return bounds;
}

} // namespace

TEST_CASE ("The Auto Gain dot shows beside the Output readout exactly while the readout includes Auto Gain, and it and the A toggle's tooltip give the amount, following it on the editor's tick, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& dot = host.footer().autoGainDot();
    auto& readout = host.outputReadout();

    // Auto Gain off: no dot, and the A toggle's tooltip is its description alone.
    dynLabDefaults (host);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::outputGainId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return readout.getButtonText() == "+1.0 dB"; }));
    CHECK_FALSE (dot.isVisible());
    host.openOutputPopover();
    auto& autoGain = host.showing<plugkit::ui::OutputToggle> ("Auto Gain");
    CHECK (autoGain.getTooltip() == autoGainDescription);

    // On, at Threshold -30 dB on Clean: dyn-lab's estimate, 7.9161 dB, as the readout's test reads it.
    host.edit (parameters::autoGainId, 1.0f);
    CHECK (host.settleUntil ([&] { return dot.isVisible(); }));
    CHECK (dot.getTooltip() == "Auto Gain: +7.9 dB");
    CHECK (host.settleUntil ([&] { return autoGain.getTooltip() == autoGainDescription + ". Now +7.9 dB"; }));

    // Moving Threshold moves the amount, and both tooltips follow what appliedAutoGainDb reports.
    host.edit (parameters::thresholdId, -12.0f);
    const auto amount = plugkit::ui::valueText::signedDb (host.processor.appliedAutoGainDb());
    REQUIRE (amount != "+7.9 dB");
    CHECK (host.settleUntil ([&] { return dot.getTooltip() == "Auto Gain: " + amount; }));
    CHECK (host.settleUntil ([&] { return autoGain.getTooltip() == autoGainDescription + ". Now " + amount; }));

    // Global Bypass draws the readout not applied, and hides the dot until it is off again.
    host.edit (parameters::globalBypassId, 1.0f);
    CHECK (host.settleUntil ([&] { return readout.isNotApplied() && ! dot.isVisible(); }));
    host.edit (parameters::globalBypassId, 0.0f);
    CHECK (host.settleUntil ([&] { return ! readout.isNotApplied() && dot.isVisible(); }));

    // Off again: no dot, and the description alone.
    host.edit (parameters::autoGainId, 0.0f);
    CHECK (host.settleUntil ([&] { return ! dot.isVisible(); }));
    CHECK (host.settleUntil ([&] { return autoGain.getTooltip() == autoGainDescription; }));
}

TEST_CASE ("With Auto Gain on, an amount that rounds to 0.0 dB, or Global Bypass on, shows no Auto Gain dot, and neither the A toggle's tooltip nor the readout's description gives an amount, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& dot = host.footer().autoGainDot();
    auto& readout = host.outputReadout();
    host.openOutputPopover();
    auto& autoGain = host.showing<plugkit::ui::OutputToggle> ("Auto Gain");

    // From an amount that reads +7.9 dB (Threshold -30 dB on Clean, as the dot's test reads it), so each wait below
    // waits for a change.
    dynLabDefaults (host);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::autoGainId, 1.0f);
    const auto showsAmount = [&] { return dot.isVisible() && autoGain.getTooltip() == autoGainDescription + ". Now +7.9 dB"; };
    REQUIRE (host.settleUntil (showsAmount));

    // Threshold 0 dB on Clean: dyn-lab's estimate, +0.0089 dB (`python3 tools/dyn-lab/dynlab.py autogain --threshold
    // 0`), above zero but 0.0 dB at one decimal.
    host.edit (parameters::thresholdId, 0.0f);
    REQUIRE (host.processor.appliedAutoGainDb() > 0.0);
    REQUIRE (host.processor.appliedAutoGainDb() < 0.05);
    CHECK (host.settleUntil ([&] { return ! dot.isVisible() && autoGain.getTooltip() == autoGainDescription; }));
    CHECK_FALSE (readout.getDescription().contains ("Auto Gain"));

    // Global Bypass on, with an amount that reads +7.9 dB: no dot, and no amount either.
    host.edit (parameters::thresholdId, -30.0f);
    REQUIRE (host.settleUntil (showsAmount));
    host.edit (parameters::globalBypassId, 1.0f);
    CHECK (host.settleUntil ([&] { return readout.isNotApplied() && autoGain.getTooltip() == autoGainDescription; }));
    CHECK_FALSE (dot.isVisible());
    CHECK_FALSE (readout.getDescription().contains ("Auto Gain"));

    // Off again: the dot and the amount are back.
    host.edit (parameters::globalBypassId, 0.0f);
    CHECK (host.settleUntil (showsAmount));
    CHECK (readout.getDescription() == "Includes Auto Gain: +7.9 dB");
}

TEST_CASE ("The Auto Gain dot sits just right of the Output readout, centred down it, and the footer's layout doesn't move as it shows and hides, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& footer = host.footer();
    auto& dot = footer.autoGainDot();
    auto& readout = host.outputReadout();

    // Output Gain +1 dB, so the readout's text keeps its width as Auto Gain's estimate is added: "+1.0 dB", "+8.9 dB".
    dynLabDefaults (host);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::outputGainId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return readout.getButtonText() == "+1.0 dB"; }));
    REQUIRE_FALSE (dot.isVisible());
    const auto hidden = layoutOf (footer);

    host.edit (parameters::autoGainId, 1.0f);
    REQUIRE (host.settleUntil ([&] { return dot.isVisible() && readout.getButtonText() == "+8.9 dB"; }));
    CHECK (layoutOf (footer) == hidden);

    // In the gap between the readout and the UI Scale menu, centred down the readout.
    const auto bounds = dot.getBounds();
    CHECK (bounds.getX() >= readout.getRight());
    CHECK (bounds.getRight() <= footer.uiScaleMenu().getX());
    CHECK (bounds.getCentreY() == readout.getBounds().getCentreY());
    CHECK (bounds.getWidth() >= static_cast<int> (comp1::AutoGainDot::diameter));
    // An area for its tooltip, but no stop for the keys or a screen reader: the readout's description says it.
    CHECK_FALSE (dot.getWantsKeyboardFocus());
    CHECK_FALSE (dot.isAccessible());
}

TEST_CASE ("While the Output readout includes Auto Gain its description says by how much, after the parallel mark's, in both views")
{
    host::EditorHost host;
    const bool compact = GENERATE (false, true);
    CAPTURE (compact);
    if (compact)
        host::EditorHost::click (host.expandedView().viewSwitch());
    auto& readout = host.outputReadout();
    dynLabDefaults (host);
    host.edit (parameters::thresholdId, -30.0f);
    host.edit (parameters::outputGainId, 1.0f);
    host.edit (parameters::dryId, off);
    REQUIRE (host.settleUntil ([&] { return readout.getButtonText() == "+1.0 dB"; }));
    CHECK (readout.getDescription().isEmpty());

    host.edit (parameters::autoGainId, 1.0f);
    CHECK (host.settleUntil ([&] { return readout.getDescription() == "Includes Auto Gain: +7.9 dB"; }));
    auto* handler = readout.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getDescription().contains ("Auto Gain: +7.9 dB"));

    host.edit (parameters::dryId, 0.0f);
    CHECK (host.settleUntil ([&] { return readout.getDescription() == "Parallel compression on: Dry is mixed in. Includes Auto Gain: +7.9 dB"; }));

    host.edit (parameters::autoGainId, 0.0f);
    CHECK (host.settleUntil ([&] { return readout.getDescription() == "Parallel compression on: Dry is mixed in"; }));
}
