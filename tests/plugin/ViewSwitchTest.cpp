// The View setting, Compact or Expanded, and the footer's switch between the views: what each view shows, its own
// window size and limits, and how the View is saved, restored and kept out of Presets.

#include "EditorHost.h"
#include "Footer.h"
#include "Parameters.h"
#include "plugkit/ui/OutputMeter.h"
#include "plugkit/ui/PresetBrowser.h"
#include "plugkit/ui/UiScaleMenu.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <optional>
#include <vector>

namespace
{

using comp1::View;

juce::Button& viewSwitch (host::EditorHost& host) { return host.expandedView().viewSwitch(); }

// A click on the switch, as the user makes it.
void clickSwitch (host::EditorHost& host) { host::EditorHost::click (viewSwitch (host)); }

// The Expanded View's own components, showing or not: the display, the meters and the sections.
std::vector<juce::Component*> expandedParts (host::EditorHost& host)
{
    auto& editor = host.expandedView();
    auto* outputMeter = harness::findChild<plugkit::ui::OutputMeter> (editor);
    auto* sections = harness::findChild<juce::Component> (editor, [] (juce::Component& c) { return c.getName() == "Sections"; });
    REQUIRE (outputMeter != nullptr);
    REQUIRE (sections != nullptr);
    return { &editor.display(),          &editor.gateGainReductionMeter(), &editor.gainReductionMeter(),
             &editor.limiterGainReductionMeter(), outputMeter,                        sections };
}

// What the editor shows: the view's own body, and the switch titled by what it does.
void checkShows (host::EditorHost& host, View view)
{
    auto& editor = host.expandedView();
    CHECK (editor.shownView() == view);
    CHECK (editor.compactView().isShowing() == (view == View::compact));
    for (auto* part : expandedParts (host))
        CHECK (part->isShowing() == (view == View::expanded));
    CHECK (editor.header().isShowing());
    const juce::String title = view == View::expanded ? "Switch to the Compact View" : "Switch to the Expanded View";
    CHECK (viewSwitch (host).isShowing());
    CHECK (viewSwitch (host).getTitle() == title);
    CHECK (viewSwitch (host).getTooltip() == title);
}

} // namespace

TEST_CASE ("A new instance opens in the Expanded View at its default size, the footer's last control the switch to the Compact View")
{
    host::EditorHost host;
    CHECK (host.processor.view() == View::expanded);
    checkShows (host, View::expanded);
    CHECK (host.editor->getWidth() == 1400);
    CHECK (host.editor->getHeight() == 860);
    auto& sw = viewSwitch (host);
    auto* footer = harness::findChild<comp1::Footer> (*host.editor);
    REQUIRE (footer != nullptr);
    CHECK (sw.getParentComponent() == footer);
    // At the far right, after the UI Scale menu.
    auto& uiScale = host.showing<juce::Component> ("UI Scale");
    CHECK (sw.getX() > uiScale.getRight());
}

TEST_CASE ("A click on the switch, or Space or Return on it, shows the other view, focus staying on the switch")
{
    host::EditorHost host;
    clickSwitch (host);
    CHECK (host.processor.view() == View::compact);
    checkShows (host, View::compact);
    clickSwitch (host);
    CHECK (host.processor.view() == View::expanded);
    checkShows (host, View::expanded);

    for (const auto key : { juce::KeyPress::spaceKey, juce::KeyPress::returnKey })
    {
        CAPTURE (key);
        viewSwitch (host).grabKeyboardFocus();
        REQUIRE (viewSwitch (host).hasKeyboardFocus (false));
        CHECK (host.press (juce::KeyPress (key)));
        checkShows (host, View::compact);
        CHECK (viewSwitch (host).hasKeyboardFocus (false));
        CHECK (host.press (juce::KeyPress (key)));
        checkShows (host, View::expanded);
        CHECK (viewSwitch (host).hasKeyboardFocus (false));
    }
}

TEST_CASE ("Switching back shows the Expanded View as it was, on the same display tab and with the Detection section open")
{
    host::EditorHost host;
    host.processor.setDisplayTab (comp1::DisplayTab::spectrum);
    host.processor.setDetectionOpen (true);
    REQUIRE (host.settleUntil ([&] {
        return host.expandedView().display().shownTab() == comp1::DisplayTab::spectrum && host.expandedView().detectionSection().isOpen();
    }));
    host.settleAnimations();
    clickSwitch (host);
    CHECK (host.processor.displayTab() == comp1::DisplayTab::spectrum);
    CHECK (host.processor.detectionOpen());
    for (auto* part : expandedParts (host))
        CHECK_FALSE (part->isShowing());
    CHECK_FALSE (host.expandedView().detectionSection().detectionAudition().isShowing());
    clickSwitch (host);
    checkShows (host, View::expanded);
    CHECK (host.expandedView().display().shownTab() == comp1::DisplayTab::spectrum);
    CHECK (host.expandedView().display().tabContent (comp1::DisplayTab::spectrum).isShowing());
    CHECK (host.expandedView().detectionSection().detectionAudition().isShowing());
}

namespace
{

// The window's size in logical pixels (as at 100%), at the UI Scale.
juce::Point<int> logicalSize (host::EditorHost& host, int percent = 100)
{
    return { juce::roundToInt (host.editor->getWidth() * 100.0 / percent), juce::roundToInt (host.editor->getHeight() * 100.0 / percent) };
}

// A resize as a host makes one: held to the window's limits.
void hostResize (host::EditorHost& host, int width, int height)
{
    auto* limits = host.editor->getConstrainer();
    REQUIRE (limits != nullptr);
    limits->setBoundsForComponent (host.editor.get(), host.editor->getBounds().withSize (width, height), false, false, true, true);
}

// The window's limits are view's own, times the UI Scale.
void checkLimits (host::EditorHost& host, View view, int percent)
{
    const auto& window = comp1::PluginProcessor::windowOf (view);
    const auto scaled = [percent] (int logical) { return juce::roundToInt (logical * percent / 100.0); };
    auto* limits = host.editor->getConstrainer();
    REQUIRE (limits != nullptr);
    CHECK (limits->getMinimumWidth() == scaled (window.minimum.x));
    CHECK (limits->getMinimumHeight() == scaled (window.minimum.y));
    CHECK (limits->getMaximumWidth() == scaled (window.maximum.x));
    CHECK (limits->getMaximumHeight() == scaled (window.maximum.y));
}

// parent's visible children inside it, none empty and none overlapping another.
void checkPlacedInside (juce::Component& parent)
{
    CAPTURE (parent.getTitle(), parent.getName());
    std::vector<juce::Rectangle<int>> placed;
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible())
            continue;
        CAPTURE (child->getTitle(), child->getName());
        CHECK_FALSE (child->getBounds().isEmpty());
        CHECK (parent.getLocalBounds().contains (child->getBounds()));
        for (const auto& other : placed)
            CHECK_FALSE (other.intersects (child->getBounds()));
        placed.push_back (child->getBounds());
    }
}

const auto& expandedWindow = comp1::PluginProcessor::expandedWindow;
const auto& compactWindow = comp1::PluginProcessor::compactWindow;

} // namespace

TEST_CASE ("Each view keeps its own window size: the Compact View opens at its default, then each at its last size, however often switched")
{
    host::EditorHost host;
    hostResize (host, 1500, 900);
    clickSwitch (host);
    CHECK (logicalSize (host) == compactWindow.defaultSize);
    hostResize (host, 1000, 640);
    clickSwitch (host);
    CHECK (logicalSize (host) == juce::Point<int> (1500, 900));
    clickSwitch (host);
    CHECK (logicalSize (host) == juce::Point<int> (1000, 640));
    for (int i = 0; i < 6; ++i)
        clickSwitch (host);
    CHECK (logicalSize (host) == juce::Point<int> (1000, 640));
    clickSwitch (host);
    CHECK (logicalSize (host) == juce::Point<int> (1500, 900));
    host.settle();
    CHECK (logicalSize (host) == juce::Point<int> (1500, 900));
    CHECK (host.processor.hiddenViewSize() == juce::Point<int> (1000, 640));
}

TEST_CASE ("At each UI Scale the window's limits are the shown view's times the scale, and a host's resize is held to them")
{
    host::EditorHost host;
    const int percent = GENERATE (75, 100, 125, 150, 200);
    CAPTURE (percent);
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (percent);
    const auto scaled = [percent] (int logical) { return juce::roundToInt (logical * percent / 100.0); };
    checkLimits (host, View::expanded, percent);
    CHECK (host.editor->getWidth() == scaled (expandedWindow.defaultSize.x));

    clickSwitch (host);
    checkLimits (host, View::compact, percent);
    CHECK (host.editor->getWidth() == scaled (compactWindow.defaultSize.x));
    CHECK (host.editor->getHeight() == scaled (compactWindow.defaultSize.y));
    hostResize (host, scaled (compactWindow.maximum.x) + 200, scaled (compactWindow.maximum.y) + 200);
    CHECK (host.editor->getWidth() == scaled (compactWindow.maximum.x));
    CHECK (host.editor->getHeight() == scaled (compactWindow.maximum.y));
    hostResize (host, 100, 100);
    CHECK (host.editor->getWidth() == scaled (compactWindow.minimum.x));
    CHECK (host.editor->getHeight() == scaled (compactWindow.minimum.y));
    // Still the Compact View's after the tick.
    host.settle();
    checkLimits (host, View::compact, percent);

    clickSwitch (host);
    checkLimits (host, View::expanded, percent);
    hostResize (host, scaled (compactWindow.minimum.x), scaled (compactWindow.minimum.y));
    CHECK (host.editor->getWidth() == scaled (expandedWindow.minimum.x));
    CHECK (host.editor->getHeight() == scaled (expandedWindow.minimum.y));

    // A UI Scale picked in the Compact View keeps its limits.
    clickSwitch (host);
    const int other = percent == 100 ? 150 : 100;
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (other);
    checkLimits (host, View::compact, other);
    host.settle();
    checkLimits (host, View::compact, other);
}

TEST_CASE ("The View and both views' sizes are saved with the session and restored, into a new instance and into an open editor")
{
    host::EditorHost host;
    hostResize (host, 1500, 900);
    clickSwitch (host);
    hostResize (host, 1000, 640);
    const auto saved = host::saved (host.processor);
    CHECK (harness::savedState (saved).getProperty ("view") == "compact");

    // Into a new instance, opened afterwards.
    host::EditorHost other;
    host::restore (other.processor, saved);
    CHECK (other.processor.view() == View::compact);
    other.reopen();
    checkShows (other, View::compact);
    CHECK (logicalSize (other) == juce::Point<int> (1000, 640));
    checkLimits (other, View::compact, 100);
    clickSwitch (other);
    CHECK (logicalSize (other) == juce::Point<int> (1500, 900));

    // Into an open editor, which follows on its tick.
    host::EditorHost open;
    host::restore (open.processor, saved);
    REQUIRE (open.settleUntil ([&] { return open.expandedView().shownView() == View::compact && logicalSize (open) == juce::Point<int> (1000, 640); }));
    checkShows (open, View::compact);
    checkLimits (open, View::compact, 100);
    clickSwitch (open);
    CHECK (logicalSize (open) == juce::Point<int> (1500, 900));

    // Saving again writes the view as the instance holds it.
    CHECK (harness::savedState (open.processor).getProperty ("view") == "expanded");
}

TEST_CASE ("A session saved without the View opens Expanded at its saved size, an unknown View restores Expanded, and a size outside its view's limits opens at the nearer limit")
{
    host::EditorHost host;
    hostResize (host, 1500, 900);
    clickSwitch (host);
    const auto saved = host::saved (host.processor);

    auto withoutView = harness::savedXml (saved);
    REQUIRE (withoutView != nullptr);
    for (const auto* name : { "view", "hidden_view_width", "hidden_view_height" })
        withoutView->removeAttribute (name);
    withoutView->setAttribute ("editorWidth", 1500);
    withoutView->setAttribute ("editorHeight", 900);
    host::EditorHost old;
    host::restore (old.processor, *withoutView);
    CHECK (old.processor.view() == View::expanded);
    old.reopen();
    checkShows (old, View::expanded);
    CHECK (logicalSize (old) == juce::Point<int> (1500, 900));
    // The Compact View, without a size of its own, at its default.
    clickSwitch (old);
    CHECK (logicalSize (old) == compactWindow.defaultSize);

    auto unknown = harness::savedXml (saved);
    REQUIRE (unknown != nullptr);
    unknown->setAttribute ("view", "tiny");
    host::restore (old.processor, *unknown);
    CHECK (old.processor.view() == View::expanded);
    REQUIRE (old.settleUntil ([&] { return old.expandedView().shownView() == View::expanded; }));

    // Compact, wider than its maximum and shorter than its minimum, and the Expanded View's size the other way round.
    auto outside = harness::savedXml (saved);
    REQUIRE (outside != nullptr);
    REQUIRE (outside->getStringAttribute ("view") == "compact");
    outside->setAttribute ("editorWidth", 2400);
    outside->setAttribute ("editorHeight", 300);
    outside->setAttribute ("hidden_view_width", 900);
    outside->setAttribute ("hidden_view_height", 9000);
    host::restore (old.processor, *outside);
    REQUIRE (old.settleUntil ([&] {
        return old.expandedView().shownView() == View::compact && logicalSize (old) == juce::Point<int> (compactWindow.maximum.x, compactWindow.minimum.y);
    }));
    clickSwitch (old);
    CHECK (logicalSize (old) == juce::Point<int> (expandedWindow.minimum.x, expandedWindow.maximum.y));
}

TEST_CASE ("The View and the hidden view's size are in no Preset, add no undo step and make no side Modified, and loading a Preset, an A/B switch or Copy and undo leave the View alone")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    auto& history = session.editHistory();
    const auto preset = session.presetState();
    REQUIRE (session.loadPreset (preset, "Start"));
    const int steps = history.undoSteps();

    clickSwitch (host);
    CHECK (harness::savedNamesContaining (session.presetState(), "view").isEmpty());
    CHECK (history.undoSteps() == steps);
    CHECK_FALSE (session.isLoadedPresetModified());
    CHECK_FALSE (harness::savedNamesContaining (harness::savedState (host.processor), "hidden_view").isEmpty());

    // An edit and its undo.
    host.edit (comp1::parameters::thresholdId, -30.0f);
    REQUIRE (history.canUndo());
    history.undo();
    CHECK (host.processor.view() == View::compact);

    // A Preset loaded.
    REQUIRE (session.loadPreset (preset, "Start"));
    host.settle();
    CHECK (host.processor.view() == View::compact);
    checkShows (host, View::compact);

    // A/B Copy and a switch.
    session.copyToOther();
    session.selectCompareSide (plugkit::ui::CompareSide::B);
    host.settle();
    CHECK (host.processor.view() == View::compact);
    checkShows (host, View::compact);
}

TEST_CASE ("A switch writes no host parameter and starts no gesture, and the audio is identical with or without switches")
{
    struct Counter final : juce::AudioProcessorParameter::Listener
    {
        int changes = 0;
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool) override { ++changes; }
    };
    // A 220 Hz tone stepping between loud and quiet every 100 ms, so the compressor moves throughout.
    const auto signal = [] (int n) {
        return (n / 4800 % 2 == 0 ? 0.8 : 0.08) * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * n / 48000.0);
    };
    const auto render = [&signal] (bool switching, int& changes) {
        host::EditorHost host;
        host.editor.reset();
        harness::useLayout (host.processor, juce::AudioChannelSet::stereo());
        host.reopen();
        host.edit (comp1::parameters::compFollowId, 0.0f);
        host.edit (comp1::parameters::thresholdId, -30.0f);
        Counter counter;
        for (auto* p : host.processor.getParameters())
            p->addListener (&counter);
        std::vector<float> out;
        for (int b = 0; b < 60; ++b)
        {
            host.play (1, 512, [&signal, b] (int i) { return signal (b * 512 + i); }, host::EditorHost::silent, [&out] (const auto& buffer) {
                for (int ch = 0; ch < 2; ++ch)
                    out.insert (out.end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + buffer.getNumSamples());
            });
            if (switching && b % 7 == 3)
            {
                clickSwitch (host);
                host.settle (16);
            }
        }
        for (auto* p : host.processor.getParameters())
            p->removeListener (&counter);
        changes = counter.changes;
        return out;
    };
    int without = 0, with = 0;
    const auto plain = render (false, without);
    const auto switched = render (true, with);
    CHECK (with == 0);
    REQUIRE (plain.size() == switched.size());
    CHECK (plain == switched);
}

TEST_CASE ("Switching to the Compact View lets go of Detection Audition")
{
    host::EditorHost host;
    host.processor.setDetectionOpen (true);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().detectionSection().isOpen(); }));
    host.settleAnimations();
    auto& audition = host.expandedView().detectionSection().detectionAudition();
    const auto centre = audition.getLocalBounds().toFloat().getCentre();
    audition.mouseDown (harness::mouseEvent (audition, centre, juce::ModifierKeys::leftButtonModifier));
    REQUIRE (host.processor.detectionAudition());
    clickSwitch (host);
    CHECK_FALSE (host.processor.detectionAudition());
    audition.mouseUp (harness::mouseEvent (audition, centre, {}, centre));
}

TEST_CASE ("Tab walks the Compact View: the header, then Clean's Compact Slots and Comp, Global Bypass, Style, the Processing Mode readout, the Stereo readout, the Saturation readout, the Output readout, UI Scale and the switch, reaching nothing of the Expanded View")
{
    host::EditorHost host;
    clickSwitch (host);
    juce::Component::unfocusAllComponents();
    std::vector<juce::Component*> walked;
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        REQUIRE (focused != nullptr);
        if (! walked.empty() && focused == walked.front())
            break;
        walked.push_back (focused);
    }
    juce::StringArray titles;
    for (auto* c : walked)
    {
        CHECK (c->isShowing());
        for (auto* part : expandedParts (host))
            CHECK_FALSE (part->isParentOf (c));
        titles.add (c->getTitle());
    }
    CAPTURE (titles.joinIntoString (", "));
    auto& header = host.expandedView().header();
    size_t inHeader = 0;
    while (inHeader < walked.size() && header.isParentOf (walked[inHeader]))
        ++inHeader;
    CHECK (inHeader > 0);
    juce::StringArray rest;
    for (auto i = inHeader; i < walked.size(); ++i)
        rest.add (walked[i]->getTitle());
    CHECK (rest
           == juce::StringArray { "Attack",       "Knee",   "Comp",       "Release", "Auto Release", "Global Bypass", "Style", "Processing Mode",
                                  "Stereo",       "Saturation", "Output", "UI Scale", "Switch to the Expanded View" });
}

TEST_CASE ("At every UI Scale and at the Compact View's minimum, default and maximum sizes, the header, the body and the footer don't overlap, nothing in the header or the footer at its widest is clipped, in every Style the body's Compact Slots, Comp and meter are inside it with each control inside its part, in the same rectangles for every Style, and the Preset browser opens inside the window with nothing in it clipped")
{
    host::EditorHost host;
    // The footer at its widest: the Output readout with the parallel mark, two digits and the Auto Gain dot.
    host.edit (comp1::parameters::autoGainId, 1.0f);
    host.edit (comp1::parameters::compId, 100.0f);
    host.edit (comp1::parameters::outputGainId, 36.0f);
    host.edit (comp1::parameters::dryId, 0.0f);
    clickSwitch (host);
    const int percent = GENERATE (75, 100, 125, 150, 200);
    CAPTURE (percent);
    host.showing<plugkit::ui::UiScaleMenu> ("UI Scale").pick (percent);
    const auto scaled = [percent] (int logical) { return juce::roundToInt (logical * percent / 100.0); };
    auto& editor = host.expandedView();
    auto& header = editor.header();
    // The wordmark's width with room to spare, which the header keeps at every size.
    hostResize (host, scaled (compactWindow.maximum.x), scaled (compactWindow.maximum.y));
    const int wordmarkWidth = header.wordmarkArea().getWidth();
    const auto size = GENERATE_COPY (compactWindow.minimum, compactWindow.defaultSize, compactWindow.maximum);
    CAPTURE (size.x, size.y);
    hostResize (host, scaled (size.x), scaled (size.y));
    REQUIRE (logicalSize (host, percent) == size);
    host.settle();
    REQUIRE (host.outputReadout().getButtonText().startsWith ("|| +"));

    auto& content = *header.getParentComponent();
    auto* footer = harness::findChild<comp1::Footer> (editor);
    REQUIRE (footer != nullptr);
    const auto in = [&content] (juce::Component& c) { return content.getLocalArea (c.getParentComponent(), c.getBounds()); };
    const juce::Rectangle<int> inside { 14, 14, size.x - 28, size.y - 28 };
    const std::vector<juce::Rectangle<int>> rows { in (header), in (editor.compactView()), in (*footer) };
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CAPTURE (i);
        CHECK (inside.contains (rows[i]));
        for (size_t j = i + 1; j < rows.size(); ++j)
            CHECK_FALSE (rows[i].intersects (rows[j]));
    }
    // The header's and the footer's controls inside them, none overlapping another, and the controls of the header's
    // groups inside their group; the wordmark whole, and the Output readout as wide as its text.
    for (auto* bar : { static_cast<juce::Component*> (&header), static_cast<juce::Component*> (footer) })
        checkPlacedInside (*bar);
    for (auto* group : header.getChildren())
        if (group->isVisible())
            checkPlacedInside (*group);
    CHECK (header.wordmarkArea().getWidth() == wordmarkWidth);
    for (auto* group : header.getChildren())
        if (group->isVisible())
            CHECK_FALSE (header.wordmarkArea().intersects (group->getBounds()));
    CHECK (host.outputReadout().getWidth() >= host.outputReadout().getIdealWidth());
    // The Stereo readout at its widest, "Stereo 100%", has room for its whole text: it doesn't shrink to its label.
    REQUIRE (host.stereoReadout().getButtonText() == "Stereo 100%");
    CHECK_FALSE (host.stereoReadout().isShrunk());
    // In every Style: the body's Compact Slots, Comp's section and its meter inside it, none overlapping another, each
    // one's controls inside it; the meter as tall as the body. Every Style's rectangles are the same: the parts', the
    // slots' primaries' and Comp's (ADR 0011).
    auto& body = editor.compactView();
    auto* compressor = harness::findChild<juce::Component> (body, [] (juce::Component& c) { return c.getName() == "Compressor"; });
    REQUIRE (compressor != nullptr);
    auto& slotA = body.compactSlot (0);
    auto& slotB = body.compactSlot (1);
    auto& comp = host.showing<staple::Knob> ("Comp");
    std::optional<std::vector<juce::Rectangle<int>>> firstStyles;
    for (const auto style : comp1::PluginProcessor::offeredStyles())
    {
        CAPTURE (static_cast<int> (style));
        REQUIRE (host.processor.chooseStyle (style));
        host.settle();
        std::vector<juce::Rectangle<int>> placed;
        for (auto* part : body.getChildren())
        {
            CAPTURE (part->getName());
            REQUIRE (part->isVisible());
            CHECK (body.getLocalBounds().contains (part->getBounds()));
            CHECK_FALSE (part->getBounds().isEmpty());
            for (const auto& other : placed)
                CHECK_FALSE (other.intersects (part->getBounds()));
            placed.push_back (part->getBounds());
            // Each control's own area: a knob's face (its shadow may reach past it), anything else its bounds.
            for (auto* control : part->getChildren())
            {
                if (! control->isVisible())
                    continue;
                CAPTURE (control->getTitle());
                auto area = control->getBounds();
                if (auto* knob = dynamic_cast<staple::Knob*> (control))
                {
                    const auto face = knob->getFaceCentre().toInt() + knob->getPosition();
                    const int radius = juce::roundToInt (knob->getFaceRadius());
                    area = juce::Rectangle<int> (2 * radius, 2 * radius).withCentre (face);
                }
                CHECK (part->getLocalBounds().contains (area));
            }
        }
        // Slot A, Comp, slot B and the meter.
        CHECK (placed.size() == 4);
        CHECK (body.gainReductionMeter().getHeight() == body.getHeight());
        // One row filling the body, left to right: slot A, Comp, slot B, then the meter at its right; the slots and
        // Comp centred down it.
        const auto meter = body.gainReductionMeter().getBounds();
        CHECK (meter.getRight() == body.getWidth());
        CHECK (slotA.getRight() < compressor->getX());
        CHECK (compressor->getRight() < slotB.getX());
        CHECK (slotB.getRight() < meter.getX());
        for (juce::Component* part : { static_cast<juce::Component*> (&slotA), compressor, static_cast<juce::Component*> (&slotB) })
        {
            CAPTURE (part->getName());
            CHECK (std::abs (part->getBounds().getCentreY() - body.getHeight() / 2) <= 1);
        }
        placed.push_back (in (body));
        placed.push_back (slotA.primary().getBounds());
        placed.push_back (slotB.primary().getBounds());
        placed.push_back (comp.getBounds());
        if (firstStyles)
            CHECK (placed == *firstStyles);
        else
            firstStyles = placed;
    }

    // The Preset browser opens over everything, its panel whole inside its margin.
    auto& presets = host.showing<juce::Button> ("Presets");
    REQUIRE (presets.onClick != nullptr);
    presets.onClick();
    host.settleAnimations();
    auto& browser = header.presetBrowser();
    REQUIRE (browser.isShowing());
    CHECK (in (browser) == content.getLocalBounds());
    // Its panel inside its margin, smaller than its full size in a smaller window, with its search row, folders, list
    // and footer actions inside it, none overlapping another, and again with Save as's name field open.
    const auto margin = browser.getLocalBounds().reduced (plugkit::ui::PresetBrowser::margin);
    for (auto* panel : browser.getChildren())
        if (panel->isVisible())
        {
            CHECK (margin.contains (panel->getBounds()));
            checkPlacedInside (*panel);
            auto* save = harness::findChild<juce::Button> (*panel, [] (juce::Button& b) { return b.getButtonText().startsWith ("Save as"); });
            REQUIRE (save != nullptr);
            host::EditorHost::click (*save);
            checkPlacedInside (*panel);
        }
}
