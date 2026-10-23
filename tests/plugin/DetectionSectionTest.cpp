// The Expanded View's Detection section: whether it is open, saved per instance as view state, and in no Preset.

#include "EditorHost.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

TEST_CASE ("The Detection section is collapsed for a new instance; whether it is open is saved with the session as detection_open and restored, collapsed for a session saved without it")
{
    host::EditorHost host;
    CHECK_FALSE (host.processor.detectionOpen());
    CHECK (harness::savedState (host.processor).getProperty ("detection_open").toString() == "collapsed");

    host.processor.setDetectionOpen (true);
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedState (saved).getProperty ("detection_open").toString() == "open");

    // Into a new instance.
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.detectionOpen());

    // Without it, and with a value it doesn't know: collapsed.
    for (const auto* value : { "", "half" })
    {
        CAPTURE (value);
        auto xml = harness::savedXml (saved);
        REQUIRE (xml != nullptr);
        if (juce::String (value).isEmpty())
            xml->removeAttribute ("detection_open");
        else
            xml->setAttribute ("detection_open", value);
        other.processor.setDetectionOpen (true);
        host::restore (other.processor, *xml);
        CHECK_FALSE (other.processor.detectionOpen());
    }
}

TEST_CASE ("Whether the Detection section is open is in no Preset, adds no undo step and makes no side Modified, and loading a Preset, undo or an A/B switch leave it alone")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    host.processor.setDetectionOpen (true);
    CHECK (harness::savedNamesContaining (session.presetState(), "detection_open").isEmpty());
    CHECK (history.undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());

    host.edit (comp1::parameters::thresholdId, -30.0f);
    REQUIRE (history.canUndo());
    history.undo();
    CHECK (host.processor.detectionOpen());

    REQUIRE (session.loadPreset (preset, "Start"));
    host.settle();
    CHECK (host.processor.detectionOpen());

    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    host.settle();
    CHECK (host.processor.detectionOpen());
}

namespace
{

comp1::DetectionSection& section (host::EditorHost& host) { return host.expandedView().detectionSection(); }

// What a screen reader reads as the disclosure's value.
juce::String disclosureValue (host::EditorHost& host)
{
    auto* handler = section (host).disclosure().getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getValueInterface() != nullptr);
    return handler->getValueInterface()->getCurrentValueAsString();
}

// The section's controls under its title: everything but the disclosure.
std::vector<juce::Component*> controlsOf (host::EditorHost& host)
{
    std::vector<juce::Component*> controls;
    for (auto* c : section (host).getChildren())
        if (c != &section (host).disclosure())
            controls.push_back (c);
    return controls;
}

// Collapsed or open, the slide over: the disclosure reads so, the controls show only while open, and the section is a
// strip of its title's height or its full height.
void checkOpen (host::EditorHost& host, bool open)
{
    CHECK (host.processor.detectionOpen() == open);
    CHECK (section (host).isOpen() == open);
    CHECK (disclosureValue (host) == (open ? "open" : "collapsed"));
    for (auto* c : controlsOf (host))
    {
        CAPTURE (c->getTitle(), c->getName());
        CHECK (c->isShowing() == open);
    }
    CHECK (section (host).getHeight() == (open ? section (host).ControlSection::idealHeight() : comp1::Section::titleHeight));
}

} // namespace

TEST_CASE ("The Detection section opens collapsed, its title a disclosure titled Detection; a click slides it open and closed, the display taking the height it gives up, the window keeping its size")
{
    host::EditorHost host;
    auto& disclosure = section (host).disclosure();
    CHECK (disclosure.getTitle() == "Detection");
    CHECK (disclosure.isShowing());
    CHECK (disclosure.getWantsKeyboardFocus());
    CHECK (disclosure.getAccessibilityHandler()->getRole() == juce::AccessibilityRole::button);
    checkOpen (host, false);
    const auto window = host.editor->getBounds();
    const auto display = host.expandedView().display().getBounds();

    host::EditorHost::click (disclosure);
    // Part-way, then open.
    host.settle (60);
    CHECK (section (host).getHeight() > comp1::Section::titleHeight);
    host.settleAnimations();
    checkOpen (host, true);
    const int grown = section (host).getHeight() - comp1::Section::titleHeight;
    CHECK (host.editor->getBounds() == window);
    // The display gives up what the section's row grew by: the row's height is its tallest section's.
    const auto opened = host.expandedView().display().getBounds();
    CHECK (opened.getHeight() < display.getHeight());
    CHECK (opened.getHeight() >= display.getHeight() - grown);
    CHECK (opened.getWidth() == display.getWidth());

    host::EditorHost::click (disclosure);
    host.settleAnimations();
    checkOpen (host, false);
    CHECK (host.editor->getBounds() == window);
    CHECK (host.expandedView().display().getBounds() == display);
}

TEST_CASE ("Return or Space on the focused disclosure toggles the Detection section, and its press action does")
{
    host::EditorHost host;
    auto& disclosure = section (host).disclosure();
    disclosure.grabKeyboardFocus();
    REQUIRE (disclosure.hasKeyboardFocus (false));
    CHECK (host.press (juce::KeyPress (juce::KeyPress::returnKey)));
    host.settleAnimations();
    checkOpen (host, true);
    CHECK (host.press (juce::KeyPress (juce::KeyPress::spaceKey)));
    host.settleAnimations();
    checkOpen (host, false);

    auto* handler = disclosure.getAccessibilityHandler();
    REQUIRE (handler->getActions().invoke (juce::AccessibilityActionType::press));
    host.settleAnimations();
    checkOpen (host, true);
}

TEST_CASE ("An open editor follows a restored Detection section, and it stays open across a view switch and a reopened editor")
{
    host::EditorHost host;
    host.processor.setDetectionOpen (true);
    const auto saved = host::saved (host.processor);
    host.processor.setDetectionOpen (false);
    host.settleAnimations();
    checkOpen (host, false);

    host::restore (host.processor, saved);
    REQUIRE (host.settleUntil ([&] { return section (host).isOpen(); }));
    host.settleAnimations();
    checkOpen (host, true);

    // To the Compact View and back.
    host::EditorHost::click (host.expandedView().viewSwitch());
    host.settle();
    host::EditorHost::click (host.expandedView().viewSwitch());
    host.settleAnimations();
    checkOpen (host, true);

    host.reopen();
    host.settleAnimations();
    checkOpen (host, true);
}

TEST_CASE ("Collapsed, the Detection section's summary names the Detection Source and each band's Frequency, off for a band that is, and follows Host Automation")
{
    host::EditorHost host;
    const juce::String dot { juce::CharPointer_UTF8 (" \xc2\xb7 ") };
    const auto summary = [&host] { return section (host).summary(); };
    CHECK (summary() == "Internal" + dot + "HP off" + dot + "LP off" + dot + "Free band off");

    host.edit (comp1::parameters::detectionSourceId, 1.0f);
    host.edit (comp1::parameters::detectionHighPassOnId, 1.0f);
    host.edit (comp1::parameters::detectionHighPassFrequencyId, 80.0f);
    host.edit (comp1::parameters::detectionBandOnId, 1.0f);
    host.edit (comp1::parameters::detectionBandFrequencyId, 2000.0f);
    const auto expected = "External" + dot + "HP 80 Hz" + dot + "LP off" + dot + "Bell 2.0 kHz";
    REQUIRE (host.settleUntil ([&] { return summary() == expected; }));

    host.edit (comp1::parameters::detectionLowPassOnId, 1.0f);
    host.edit (comp1::parameters::detectionLowPassFrequencyId, 12500.0f);
    host.edit (comp1::parameters::detectionBandShapeId, 2.0f);
    const auto after = "External" + dot + "HP 80 Hz" + dot + "LP 12.5 kHz" + dot + "High Shelf 2.0 kHz";
    REQUIRE (host.settleUntil ([&] { return summary() == after; }));
}

TEST_CASE ("Tab reaches the disclosure after the Character and Saturation section's Drive, then the Detection section's controls only while it is open, then the Stereo section")
{
    host::EditorHost host;
    const bool open = GENERATE (false, true);
    CAPTURE (open);
    host.processor.setDetectionOpen (open);
    REQUIRE (host.settleUntil ([&] { return section (host).isOpen() == open; }));
    host.settleAnimations();
    auto& disclosure = section (host).disclosure();
    disclosure.grabKeyboardFocus();
    REQUIRE (disclosure.hasKeyboardFocus (false));
    REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
    auto* next = juce::Component::getCurrentlyFocusedComponent();
    REQUIRE (next != nullptr);
    CHECK (next->getTitle() == (open ? "Detection Source" : "Stereo Placement"));

    REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
    auto* before = juce::Component::getCurrentlyFocusedComponent();
    REQUIRE (before != nullptr);
    CHECK (before->getTitle() == "Drive");
}
