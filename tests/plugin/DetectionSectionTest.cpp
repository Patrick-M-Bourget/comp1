// The Expanded View's Detection section: whether it is open, saved per instance as view state, and in no Preset; its
// controls; and Detection Audition.

#include "Playing.h"

#include <cmath>
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
    // Sliding: open at once, its height still the strip's until the slide's frames grow it.
    CHECK (section (host).isOpen());
    CHECK (staple::Tween::numberEasing() > 0);
    CHECK (section (host).getHeight() == comp1::Section::titleHeight);
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

TEST_CASE ("Tab reaches the disclosure after the Character section's Colour, then the Detection section's controls only while it is open, then the Stereo section")
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
    CHECK (before->getTitle() == "Colour");
}

TEST_CASE ("Open, the Detection section shows its controls and Detection Audition after them: Tab reaches Detection Audition after Detection Free Band Q, then the Stereo section")
{
    host::EditorHost host;
    host.processor.setDetectionOpen (true);
    REQUIRE (host.settleUntil ([&] { return section (host).isOpen(); }));
    host.settleAnimations();

    auto& audition = section (host).detectionAudition();
    CHECK (audition.isShowing());
    CHECK (audition.getParentComponent() == &section (host));
    auto* handler = audition.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getTitle() == "Detection Audition");
    CHECK (handler->getRole() == juce::AccessibilityRole::button);

    // Right of the last control, inside the section, which is only as wide as the controls and the button need.
    const auto qs = host.findAll<juce::Component> ([] (juce::Component& c) { return c.getTitle() == "Detection Free Band Q"; });
    REQUIRE (qs.size() == 1);
    CHECK (audition.getX() > qs.front()->getRight());
    CHECK (section (host).getLocalBounds().contains (audition.getBounds()));
    CHECK (section (host).idealWidth() == audition.getRight());

    juce::Component::unfocusAllComponents();
    section (host).disclosure().grabKeyboardFocus();
    juce::StringArray titles;
    for (int i = 0; i < 40; ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        REQUIRE (focused != nullptr);
        titles.add (focused->getTitle());
        if (focused->getTitle() == "Stereo Placement")
            break;
    }
    CAPTURE (titles.joinIntoString (", "));
    const int last = titles.indexOf ("Detection Free Band Q");
    REQUIRE (last >= 0);
    CHECK (titles[last + 1] == "Detection Audition");
    CHECK (titles[last + 2] == "Stereo Placement");
}

namespace
{

constexpr double testRate = 48000.0;

// A sine at frequency, at amplitude, at 48 kHz: sample n.
double sine (double frequency, double amplitude, int n)
{
    return amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * n / testRate);
}

// The editor playing (Playing.h) on a stereo track with a stereo Sidechain, the Detection section open.
struct OpenDetectionHost : host::Playing
{
    OpenDetectionHost() : host::Playing (testRate, juce::AudioChannelSet::stereo()) { openDetection (true); }

    // Opens or collapses the Detection section, as a click on its disclosure does, and waits for the slide's end.
    void openDetection (bool open)
    {
        auto& detection = expandedView().detectionSection();
        if (detection.isOpen() != open)
            click (detection.disclosure());
        REQUIRE (detection.isOpen() == open);
        settleAnimations();
    }

    staple::IconButton& audition() { return expandedView().detectionSection().detectionAudition(); }
};

// The loudest sample of a block.
float peakOf (const juce::AudioBuffer<float>& block)
{
    float peak = 0.0f;
    for (int ch = 0; ch < block.getNumChannels(); ++ch)
        peak = std::max (peak, block.getMagnitude (ch, 0, block.getNumSamples()));
    return peak;
}

void press (staple::IconButton& button)
{
    button.mouseDown (harness::mouseEvent (button, button.getLocalBounds().toFloat().getCentre(), juce::ModifierKeys::leftButtonModifier));
}

void release (staple::IconButton& button)
{
    const auto centre = button.getLocalBounds().toFloat().getCentre();
    button.mouseUp (harness::mouseEvent (button, centre, {}, centre));
}

} // namespace

TEST_CASE ("Detection Audition, held by the mouse or Space, plays the detection signal, and lets go on release, as the Detection section collapses, as the editor's window hides or the editor closes, and on a restore")
{
    OpenDetectionHost host;
    auto& session = host.processor.session();
    // External, the Sidechain carrying a 100 Hz sine and the main input silence: only Detection Audition plays it.
    host.edit (comp1::parameters::detectionSourceId, 1.0f);
    REQUIRE (session.loadPreset (session.presetState(), "External"));
    const int steps = session.editHistory().undoSteps();
    const auto silence = [] (int) { return 0.0; };
    const auto tone = [] (int n) { return sine (100.0, 0.5, n); };
    CHECK (peakOf (host.play (20, 512, silence, tone)) < 1.0e-4f);

    auto& audition = host.audition();
    press (audition);
    CHECK (host.processor.detectionAudition());
    CHECK (audition.isLit());
    CHECK (peakOf (host.play (20, 512, silence, tone)) > 0.4f);
    release (audition);
    CHECK_FALSE (host.processor.detectionAudition());
    CHECK_FALSE (audition.isLit());

    // Space, while focused: held while the key is.
    audition.grabKeyboardFocus();
    REQUIRE (audition.hasKeyboardFocus (false));
    REQUIRE (host.hold (juce::KeyPress (juce::KeyPress::spaceKey)));
    CHECK (host.processor.detectionAudition());
    CHECK (audition.isLit());
    host.release();
    CHECK_FALSE (host.processor.detectionAudition());

    // Not an undo step, nothing Modified, and not saved.
    press (audition);
    CHECK (session.editHistory().undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedNamesContaining (harness::savedState (saved), "audition").isEmpty());

    // Collapsing lets go, as the slide starts.
    host::EditorHost::click (host.expandedView().detectionSection().disclosure());
    CHECK_FALSE (host.processor.detectionAudition());
    host.settleAnimations();
    host.openDetection (true);

    // A restore lets go, and the button follows.
    press (audition);
    REQUIRE (host.processor.detectionAudition());
    host::restore (host.processor, saved);
    CHECK_FALSE (host.processor.detectionAudition());
    REQUIRE (host.settleUntil ([&] { return ! audition.isLit(); }));

    // Hiding the editor's window, without closing the editor, lets go.
    press (audition);
    REQUIRE (host.processor.detectionAudition());
    host.editor->setVisible (false);
    CHECK (host.settleUntil ([&] { return ! host.processor.detectionAudition(); }));
    CHECK_FALSE (audition.isLit());
    host.editor->setVisible (true);

    // Closing the editor lets go.
    press (host.audition());
    REQUIRE (host.processor.detectionAudition());
    host.editor.reset();
    CHECK_FALSE (host.processor.detectionAudition());
}

TEST_CASE ("Expanded View snapshots of the Detection section: collapsed, open with bands on, and open with Detection Audition held", "[.screens]")
{
    {
        host::EditorHost host;
        host.settleAnimations();
        harness::writeSnapshot (*host.editor, "expanded-detection-collapsed", {}, 1.0f);
    }
    OpenDetectionHost host;
    host.edit (comp1::parameters::detectionHighPassOnId, 1.0f);
    host.edit (comp1::parameters::detectionHighPassFrequencyId, 120.0f);
    host.edit (comp1::parameters::detectionBandOnId, 1.0f);
    host.edit (comp1::parameters::detectionBandGainId, 6.0f);
    host.settleAnimations();
    harness::writeSnapshot (*host.editor, "expanded-detection-open", {}, 1.0f);
    press (host.audition());
    REQUIRE (host.processor.detectionAudition());
    host.settleAnimations();
    harness::writeSnapshot (*host.editor, "expanded-detection-audition", {}, 1.0f);
    release (host.audition());
}
