// The Expanded View's controls: one for each host parameter, what a screen reader and the tooltips read, Tab's order and
// the keys; and the Compact View's: its five host parameters' controls, the names a screen reader reads and Tab's
// order.

#include "EditorHost.h"
#include "Footer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <vector>

namespace parameters = comp1::parameters;

namespace
{

// What a user operates: the knobs, buttons, choices and chips.
bool isControl (juce::Component& c)
{
    return dynamic_cast<juce::Slider*> (&c) != nullptr || dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr;
}

// The showing controls titled title: the shown view's, the header's and the footer's.
std::vector<juce::Component*> controlsTitled (host::EditorHost& host, const juce::String& title)
{
    return host.findAll<juce::Component> ([&title] (juce::Component& c) { return isControl (c) && c.isShowing() && c.getTitle() == title; });
}

// Every control can be: Comp Follow off (so Auto Gain is enabled), High Quality (so Lookahead and Oversampling are),
// on a stereo track (so Stereo Placement and Stereo Link are).
void everythingAvailable (host::EditorHost& host)
{
    host.edit (parameters::processingModeId, 1.0f);
    host.edit (parameters::compFollowId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return host.showing<juce::ToggleButton> ("Auto Gain").isEnabled() && host.knob ("Lookahead").isEnabled(); }));
}

float plainOf (juce::RangedAudioParameter& parameter) { return parameter.convertFrom0to1 (parameter.getValue()); }

// A key pressed with control focused, the editor's window taking it as the user's.
bool pressIn (host::EditorHost& host, juce::Component& control, const juce::KeyPress& key)
{
    control.grabKeyboardFocus();
    REQUIRE (control.hasKeyboardFocus (false));
    return host.press (key);
}

// Changes parameter's control by the mouse, as the user does: a drag, a click, a pick from its menu.
void changeByMouse (juce::Component& control, juce::RangedAudioParameter& parameter)
{
    if (auto* knob = dynamic_cast<staple::Knob*> (&control))
        host::EditorHost::drag (*knob, parameter.getValue() < 0.5f ? 60.0f : -60.0f);
    else if (auto* chip = dynamic_cast<comp1::ChoiceChip*> (&control))
    {
        const auto menu = chip->menu();
        juce::String other;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (! it.getItem().isTicked)
                other = it.getItem().text;
        auto action = harness::actionOf (menu, other);
        REQUIRE (action != nullptr);
        action();
    }
    else if (auto* button = dynamic_cast<juce::Button*> (&control))
        host::EditorHost::click (*button);
    else if (auto* box = dynamic_cast<juce::ComboBox*> (&control))
        box->setSelectedItemIndex (box->getSelectedItemIndex() == 0 ? 1 : 0, juce::sendNotificationSync);
}

// Changes it by the keys: an arrow for a knob or a choice, Space for a button.
void changeByKeys (host::EditorHost& host, juce::Component& control, juce::RangedAudioParameter& parameter)
{
    if (dynamic_cast<staple::Knob*> (&control) != nullptr || dynamic_cast<juce::ComboBox*> (&control) != nullptr
        || dynamic_cast<comp1::ChoiceChip*> (&control) != nullptr)
    {
        const float before = parameter.getValue();
        pressIn (host, control, juce::KeyPress (juce::KeyPress::upKey));
        if (juce::exactlyEqual (parameter.getValue(), before))
            pressIn (host, control, juce::KeyPress (juce::KeyPress::downKey));
    }
    else
        pressIn (host, control, juce::KeyPress (juce::KeyPress::spaceKey));
}

// Every element showing that a screen reader is told about and the user operates or reads (a button, toggle, choice,
// slider, text field or meter), by its title: the components carrying each.
std::map<juce::String, std::vector<juce::Component*>> accessibleNames (juce::Component& editor)
{
    using Role = juce::AccessibilityRole;
    std::map<juce::String, std::vector<juce::Component*>> names;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (! child->isShowing() || ! child->isAccessible())
                continue;
            if (auto* handler = child->getAccessibilityHandler())
            {
                const auto role = handler->getRole();
                if (role == Role::button || role == Role::toggleButton || role == Role::radioButton || role == Role::comboBox
                    || role == Role::slider || role == Role::editableText || role == Role::progressBar
                    || role == Role::image)
                    names[handler->getTitle()].push_back (child);
            }
            visit (*child);
        }
    };
    visit (editor);
    return names;
}

// GLOSSARY.md's terms (its bold headings, "Dry / Wet" as two) and the words it lists under _Avoid_, leaving out any
// clause of them a parenthesis qualifies ("Threshold, ... or Hold (unqualified) for the Gate's"): those are avoided only
// where the qualifier says.
struct Glossary
{
    juce::StringArray terms;
    std::vector<juce::String> avoided;
};

Glossary readGlossary()
{
    const juce::File file { COMP1_GLOSSARY };
    REQUIRE (file.existsAsFile());
    Glossary glossary;
    for (const auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
    {
        if (line.startsWith ("**"))
            for (auto term : juce::StringArray::fromTokens (line.fromFirstOccurrenceOf ("**", false, false).upToLastOccurrenceOf ("**", false, false), "/", {}))
                glossary.terms.add (term.removeCharacters ("*").trim());
        if (! line.startsWith ("_Avoid_:"))
            continue;
        for (const auto& clause : juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (":", false, false), ";", {}))
        {
            if (clause.containsChar ('('))
                continue;
            for (auto word : juce::StringArray::fromTokens (clause.replace (" or ", ","), ",", {}))
                if (word.trim().isNotEmpty())
                    glossary.avoided.push_back (word.trim());
        }
    }
    // Longest first, so a longer term goes before one inside it.
    std::sort (glossary.terms.begin(), glossary.terms.end(), [] (const auto& a, const auto& b) { return a.length() > b.length(); });
    return glossary;
}

// The avoided word name uses outside the glossary's own terms, or nothing.
juce::String avoidedIn (const juce::String& name, const Glossary& glossary)
{
    auto rest = " " + name.toLowerCase() + " ";
    for (const auto& term : glossary.terms)
        rest = rest.replace (term.toLowerCase(), " ");
    for (const auto& word : glossary.avoided)
    {
        const std::regex whole ("(^|[^a-z])" + word.toLowerCase().toStdString() + "([^a-z]|$)");
        if (std::regex_search (rest.toStdString(), whole))
            return word;
    }
    return {};
}

} // namespace

TEST_CASE ("Every host parameter has exactly one control, titled with its name, that changes it by mouse and by keys")
{
    host::EditorHost host;
    everythingAvailable (host);
    for (auto* each : host.processor.getParameters())
    {
        auto& parameter = dynamic_cast<juce::RangedAudioParameter&> (*each);
        const auto name = parameter.getName (100);
        CAPTURE (name);
        const auto controls = controlsTitled (host, name);
        REQUIRE (controls.size() == 1);
        auto& control = *controls.front();
        REQUIRE (control.isShowing());
        REQUIRE (control.isEnabled());

        const float before = parameter.getValue();
        changeByMouse (control, parameter);
        CHECK_FALSE (juce::exactlyEqual (parameter.getValue(), before));
        host.settle();

        const float byMouse = parameter.getValue();
        changeByKeys (host, control, parameter);
        CHECK_FALSE (juce::exactlyEqual (parameter.getValue(), byMouse));
        // Back where it was for the next, so Comp Follow and Processing Mode leave the rest available.
        host.edit (parameter.getParameterID(), parameter.convertFrom0to1 (before));
        everythingAvailable (host);
    }
}

TEST_CASE ("Every control in the editor has an accessible name, none a word the glossary avoids, on stereo and on mono")
{
    host::EditorHost host;
    const auto channels = GENERATE (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono());
    CAPTURE (channels.getDescription());
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
    // The Preset browser's names too, with a User Preset in it.
    auto& session = host.processor.session();
    REQUIRE (session.presetLibrary().save ("Lead Vocal", session.presetState()));
    auto& presets = host.showing<juce::Button> ("Presets");
    REQUIRE (presets.onClick != nullptr);
    presets.onClick();
    host.settleAnimations();

    auto names = accessibleNames (*host.editor);
    // The Detection EQ tab's too: its handles and Detection Audition.
    host.processor.setDisplayTab (comp1::DisplayTab::detectionEq);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == comp1::DisplayTab::detectionEq; }));
    for (const auto& [name, components] : accessibleNames (*host.editor))
        names[name] = components;
    // And the Transfer Curve tab's: its graph.
    host.processor.setDisplayTab (comp1::DisplayTab::transferCurve);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == comp1::DisplayTab::transferCurve; }));
    for (const auto& [name, components] : accessibleNames (*host.editor))
        names[name] = components;
    const auto glossary = readGlossary();
    REQUIRE_FALSE (glossary.avoided.empty());
    std::vector<juce::String> listed;
    for (const auto& [name, components] : names)
    {
        CAPTURE (name, components.size());
        CHECK (name.isNotEmpty());
        CHECK (avoidedIn (name, glossary).isEmpty());
        // Titled on purpose, not by JUCE falling back to a button's text.
        for (auto* c : components)
            CHECK (c->getTitle() == name);
        listed.push_back (name);
    }
    // Every name, for the reviewer to check against GLOSSARY.md, in order: one Clip Light on mono, two on stereo.
    std::vector<juce::String> expected;
    expected.push_back ("A/B Compare");
    expected.push_back ("Attack");
    expected.push_back ("Auto Gain");
    expected.push_back ("Auto Release");
    expected.push_back ("Ceiling");
    if (channels == juce::AudioChannelSet::stereo())
    {
        expected.push_back ("Clip Light Left");
        expected.push_back ("Clip Light Right");
    }
    else
        expected.push_back ("Clip Light");
    expected.push_back ("Close Preset browser");
    expected.push_back ("Colour");
    expected.push_back ("Comp");
    expected.push_back ("Comp Follow");
    expected.push_back ("Copy A to B");
    expected.push_back ("Detection Audition");
    expected.push_back ("Detection EQ");
    expected.push_back ("Detection Free Band");
    expected.push_back ("Detection Free Band Frequency");
    expected.push_back ("Detection Free Band Gain");
    expected.push_back ("Detection Free Band Q");
    expected.push_back ("Detection Free Band Shape");
    expected.push_back ("Detection High-Pass");
    expected.push_back ("Detection High-Pass Frequency");
    expected.push_back ("Detection High-Pass Slope");
    expected.push_back ("Detection Low-Pass");
    expected.push_back ("Detection Low-Pass Frequency");
    expected.push_back ("Detection Low-Pass Slope");
    expected.push_back ("Detection Source");
    expected.push_back ("Drive");
    expected.push_back ("Dry");
    expected.push_back ("Factory");
    expected.push_back ("Free band handle");
    expected.push_back ("Gain Reduction");
    expected.push_back ("Gate");
    expected.push_back ("Gate Attack");
    expected.push_back ("Gate Gain Reduction");
    expected.push_back ("Gate Hold");
    expected.push_back ("Gate Range");
    expected.push_back ("Gate Release");
    expected.push_back ("Gate Threshold");
    expected.push_back ("Global Bypass");
    expected.push_back ("High-pass handle");
    expected.push_back ("Hold");
    expected.push_back ("Knee");
    expected.push_back ("Level");
    expected.push_back ("Level and Gain Reduction Graph");
    expected.push_back ("Limiter");
    expected.push_back ("Limiter Gain Reduction");
    expected.push_back (juce::String (juce::CharPointer_UTF8 ("Load Preset File" "\xe2\x80\xa6")));
    expected.push_back ("Lookahead");
    expected.push_back ("Low-pass handle");
    expected.push_back ("Next Preset");
    expected.push_back ("Output Gain");
    expected.push_back ("Oversampling");
    expected.push_back ("Presets");
    expected.push_back ("Previous Preset");
    expected.push_back ("Processing Mode");
    expected.push_back ("Range");
    expected.push_back ("Ratio");
    expected.push_back ("Redo");
    expected.push_back ("Release");
    expected.push_back ("Saturation");
    expected.push_back ("Saturation Position");
    expected.push_back (juce::String (juce::CharPointer_UTF8 ("Save as" "\xe2\x80\xa6")));
    expected.push_back ("Search Presets");
    expected.push_back ("Show User Presets Folder");
    expected.push_back ("Stereo Link");
    expected.push_back ("Stereo Placement");
    expected.push_back ("Style");
    expected.push_back ("Switch to the Compact View");
    expected.push_back ("Threshold");
    expected.push_back ("Transfer Curve");
    expected.push_back ("Transfer Curve Graph");
    expected.push_back ("UI Scale");
    expected.push_back ("Undo");
    expected.push_back ("User");
    expected.push_back ("Wet");
    CHECK (listed == expected);
}

TEST_CASE ("Each knob reads its value with its unit as its tooltip shows it: Ratio as 4:1 and infinity:1, Ceiling in dBFS, Dry and Wet Off at the bottom")
{
    host::EditorHost host;
    everythingAvailable (host);
    for (auto* knob : host.findAll<staple::Knob>())
    {
        CAPTURE (knob->getTitle());
        auto* handler = knob->getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        REQUIRE (handler->getValueInterface() != nullptr);
        CHECK (handler->getValueInterface()->getCurrentValueAsString() == knob->tooltipValue());
        CHECK (knob->tooltipTitle() == knob->getTitle());
    }

    const auto spoken = [&host] (const juce::String& title) { return host.knob (title).getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString(); };
    host.edit (parameters::ratioId, 4.0f);
    REQUIRE (host.settleUntil ([&] { return spoken ("Ratio") == "4:1"; }));
    host.edit (parameters::ratioId, std::numeric_limits<float>::infinity());
    REQUIRE (host.settleUntil ([&] { return spoken ("Ratio") == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e:1")); }));
    host.edit (parameters::ceilingId, -3.0f);
    REQUIRE (host.settleUntil ([&] { return spoken ("Ceiling") == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "3.00 dBFS")); }));
    host.edit (parameters::dryId, -std::numeric_limits<float>::infinity());
    host.edit (parameters::wetId, -std::numeric_limits<float>::infinity());
    REQUIRE (host.settleUntil ([&] { return spoken ("Dry") == "Off" && spoken ("Wet") == "Off"; }));
    CHECK (host.knob ("Dry").tooltipValue() == "Off");

    // What it reads, typed back, sets it.
    auto& ratio = host.knob ("Ratio");
    REQUIRE (ratio.commitTypedText ("2.5:1"));
    CHECK_THAT (host.value (parameters::ratioId), Catch::Matchers::WithinRel (2.5f, 1.0e-5f));
    REQUIRE (ratio.commitTypedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e:1"))));
    CHECK (std::isinf (host.value (parameters::ratioId)));
    REQUIRE (host.knob ("Ceiling").commitTypedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "6.00 dBFS"))));
    CHECK_THAT (host.value (parameters::ceilingId), Catch::Matchers::WithinAbs (-6.0f, 1.0e-4));
    REQUIRE (host.knob ("Wet").commitTypedText ("-3 dB"));
    CHECK_THAT (host.value (parameters::wetId), Catch::Matchers::WithinAbs (-3.0f, 1.0e-4));
}

TEST_CASE ("Tab walks the header, the display's tabs, the Output Meter, the sections in signal order and the footer, reaching every enabled control once with a focus ring; Shift+Tab walks it back")
{
    host::EditorHost host;
    everythingAvailable (host);
    // On the Detection EQ tab, so its handles and Detection Audition are reached too.
    host.processor.setDisplayTab (comp1::DisplayTab::detectionEq);
    REQUIRE (host.settleUntil ([&] { return host.expandedView().display().shownTab() == comp1::DisplayTab::detectionEq; }));
    juce::Component::unfocusAllComponents();
    std::vector<juce::Component*> walked;
    for (int i = 0; i < 200; ++i)
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
        CAPTURE (c->getTitle());
        CHECK (c->hasFocusOutline());
        titles.add (c->getTitle());
    }
    CAPTURE (titles.joinIntoString (", "));
    // Each enabled control once.
    for (auto* control : host.findAll<juce::Component> ([] (juce::Component& c) { return isControl (c) && c.isShowing() && c.isEnabled() && c.getWantsKeyboardFocus(); }))
    {
        CAPTURE (control->getTitle());
        CHECK (std::count (walked.begin(), walked.end(), control) == 1);
    }
    // In order: the header's first, then these.
    const juce::StringArray order { "Presets", "Level", "Detection EQ", "Transfer Curve", "High-pass handle", "Free band handle", "Low-pass handle",
                                    "Detection Audition", "Output Meter", "Style", "Gate", "Gate Hold", "Comp Follow", "Comp",
                                    "Threshold", "Range", "Colour", "Saturation Position", "Dry", "Wet", "Output Gain", "Ceiling",
                                    "Detection Source", "Detection Free Band Q", "Stereo Placement", "Stereo Link", "Lookahead",
                                    "Global Bypass", "Processing Mode", "Oversampling", "UI Scale", "Switch to the Compact View" };
    int last = -1;
    for (const auto& title : order)
    {
        CAPTURE (title);
        const int at = titles.indexOf (title, false, last + 1);
        CHECK (at > last);
        last = std::max (last, at);
    }

    // Shift+Tab, backwards.
    std::vector<juce::Component*> back;
    for (size_t i = 0; i < walked.size(); ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
        back.push_back (juce::Component::getCurrentlyFocusedComponent());
    }
    // Focus is on the first again, so it starts from the last.
    CHECK (back == std::vector<juce::Component*> (walked.rbegin(), walked.rend()));
}

TEST_CASE ("A disabled control is skipped by Tab")
{
    host::EditorHost host;
    // Zero Latency: Lookahead and Oversampling disabled.
    host.edit (parameters::processingModeId, 0.0f);
    REQUIRE (host.settleUntil ([&] { return ! host.knob ("Lookahead").isEnabled(); }));
    juce::Component::unfocusAllComponents();
    for (int i = 0; i < 200; ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey)));
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        REQUIRE (focused != nullptr);
        CHECK (focused->getTitle() != "Lookahead");
        CHECK (focused->getTitle() != "Oversampling");
        if (focused->getTitle() == "UI Scale")
            break;
    }
}

TEST_CASE ("The arrow keys step every knob within its range, a press or a held key with its repeats being one undo step")
{
    host::EditorHost host;
    everythingAvailable (host);
    auto& history = host.processor.session().editHistory();
    for (auto* knob : host.findAll<staple::Knob> ([] (staple::Knob& k) { return k.isShowing(); }))
    {
        CAPTURE (knob->getTitle());
        knob->grabKeyboardFocus();
        REQUIRE (knob->hasKeyboardFocus (false));
        // To the top and past it: it stays at the top.
        knob->setValue (knob->getMaximum(), juce::sendNotificationSync);
        host.settle();
        int steps = history.undoSteps();
        CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
        CHECK (juce::exactlyEqual (knob->getValue(), knob->getMaximum()));
        // Down, held with two repeats: three steps down, one undo step.
        steps = history.undoSteps();
        for (int i = 0; i < 3; ++i)
            CHECK (host.hold (juce::KeyPress (juce::KeyPress::downKey)));
        host.release();
        CHECK (knob->getValue() < knob->getMaximum());
        CHECK (history.undoSteps() == steps + 1);
        // To the bottom and past it.
        knob->setValue (knob->getMinimum(), juce::sendNotificationSync);
        CHECK (host.press (juce::KeyPress (juce::KeyPress::downKey)));
        CHECK (juce::exactlyEqual (knob->getValue(), knob->getMinimum()));
        knob->setValue (knob->getDoubleClickReturnValue(), juce::sendNotificationSync);
        everythingAvailable (host);
    }
}

TEST_CASE ("Space and Return toggle a toggle, and up and down step a choice, one undo step each")
{
    host::EditorHost host;
    everythingAvailable (host);
    auto& history = host.processor.session().editHistory();

    const juce::String toggle = GENERATE (as<juce::String> {}, "Gate", "Auto Release", "Saturation", "Limiter", "Detection High-Pass", "Detection Free Band");
    CAPTURE (toggle);
    auto& button = host.showing<juce::ToggleButton> (toggle);
    const bool was = button.getToggleState();
    int steps = history.undoSteps();
    pressIn (host, button, juce::KeyPress (juce::KeyPress::spaceKey));
    CHECK (button.getToggleState() != was);
    CHECK (history.undoSteps() == steps + 1);
    pressIn (host, button, juce::KeyPress (juce::KeyPress::returnKey));
    CHECK (button.getToggleState() == was);
    CHECK (history.undoSteps() == steps + 2);

    // Processing Mode last: its step back up is Zero Latency, where Oversampling is disabled.
    for (const juce::String choice : { "Saturation Position", "Detection Source", "Detection Free Band Shape", "Stereo Placement", "Oversampling", "Processing Mode" })
    {
        CAPTURE (choice);
        auto& id = choice == "Detection Free Band Shape" ? parameters::detectionBandShapeId
                   : choice == "Stereo Placement"        ? parameters::stereoPlacementId
                   : choice == "Saturation Position"     ? parameters::saturationPositionId
                   : choice == "Detection Source"        ? parameters::detectionSourceId
                   : choice == "Processing Mode"         ? parameters::processingModeId
                                                         : parameters::oversamplingId;
        auto& parameter = host.parameter (id);
        auto& control = *controlsTitled (host, choice).front();
        // From its first choice, down to the next and back up.
        host.edit (id, 0.0f);
        host.settle();
        steps = history.undoSteps();
        pressIn (host, control, juce::KeyPress (juce::KeyPress::downKey));
        CHECK (plainOf (parameter) == 1.0f);
        CHECK (history.undoSteps() == steps + 1);
        pressIn (host, control, juce::KeyPress (juce::KeyPress::upKey));
        CHECK (plainOf (parameter) == 0.0f);
        CHECK (history.undoSteps() == steps + 2);
    }
}

TEST_CASE ("Up and down held on a footer chip step its choice, one undo step with their repeats, as on a choice; left and right don't step it")
{
    host::EditorHost host;
    everythingAvailable (host);
    auto& history = host.processor.session().editHistory();
    auto& parameter = host.parameter (parameters::oversamplingId);
    auto& chip = host.showing<comp1::ChoiceChip> ("Oversampling");
    host.edit (parameters::oversamplingId, 0.0f);
    host.settle();
    chip.grabKeyboardFocus();
    REQUIRE (chip.hasKeyboardFocus (false));

    // Down, held with a repeat: two steps, one undo step.
    int steps = history.undoSteps();
    CHECK (host.hold (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (host.hold (juce::KeyPress (juce::KeyPress::downKey)));
    host.release();
    CHECK (plainOf (parameter) == 2.0f);
    CHECK (history.undoSteps() == steps + 1);
    // Then up, pressed and released: a step of its own.
    CHECK (host.press (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (plainOf (parameter) == 1.0f);
    CHECK (history.undoSteps() == steps + 2);

    // Left and right leave it.
    steps = history.undoSteps();
    host.press (juce::KeyPress (juce::KeyPress::rightKey));
    host.press (juce::KeyPress (juce::KeyPress::leftKey));
    CHECK (plainOf (parameter) == 1.0f);
    CHECK (history.undoSteps() == steps);
}

namespace
{

// The Compact View shown, by the footer's switch, in High Quality so Oversampling is enabled.
void showCompactView (host::EditorHost& host)
{
    host.edit (parameters::processingModeId, 1.0f);
    host::EditorHost::click (host.expandedView().viewSwitch());
    REQUIRE (host.settleUntil ([&] {
        return host.expandedView().shownView() == comp1::View::compact && host.showing<juce::Component> ("Oversampling").isEnabled();
    }));
}

// The host parameters with a control in the Compact View: its own five, then the footer's.
const std::set<juce::String> compactParameters { "Style",       "Gate",          "Gate Threshold",  "Comp",
                                                 "Output Gain", "Global Bypass", "Processing Mode", "Oversampling" };

} // namespace

TEST_CASE ("In the Compact View, Style, Gate, Gate Threshold, Comp, Output Gain and the footer's three each have exactly one showing control, titled with its name, that changes it by mouse and by keys; no other host parameter has one")
{
    host::EditorHost host;
    showCompactView (host);
    std::set<juce::String> found;
    for (auto* each : host.processor.getParameters())
    {
        auto& parameter = dynamic_cast<juce::RangedAudioParameter&> (*each);
        const auto name = parameter.getName (100);
        CAPTURE (name);
        const auto controls = controlsTitled (host, name);
        if (! compactParameters.contains (name))
        {
            CHECK (controls.empty());
            continue;
        }
        found.insert (name);
        REQUIRE (controls.size() == 1);
        auto& control = *controls.front();
        REQUIRE (control.isEnabled());

        const float before = parameter.getValue();
        changeByMouse (control, parameter);
        CHECK_FALSE (juce::exactlyEqual (parameter.getValue(), before));
        host.settle();

        const float byMouse = parameter.getValue();
        changeByKeys (host, control, parameter);
        CHECK_FALSE (juce::exactlyEqual (parameter.getValue(), byMouse));
        // Back where it was for the next, in High Quality.
        host.edit (parameter.getParameterID(), parameter.convertFrom0to1 (before));
        host.edit (parameters::processingModeId, 1.0f);
        REQUIRE (host.settleUntil ([&] { return host.showing<juce::Component> ("Oversampling").isEnabled(); }));
    }
    CHECK (found == compactParameters);
}

TEST_CASE ("Every control in the Compact View has an accessible name, its own title, none a word the glossary avoids, on stereo and on mono")
{
    host::EditorHost host;
    const auto channels = GENERATE (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono());
    CAPTURE (channels.getDescription());
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
    showCompactView (host);
    // The Preset browser's names too, with a User Preset in it.
    auto& session = host.processor.session();
    REQUIRE (session.presetLibrary().save ("Lead Vocal", session.presetState()));
    auto& presets = host.showing<juce::Button> ("Presets");
    REQUIRE (presets.onClick != nullptr);
    presets.onClick();
    host.settleAnimations();

    const auto glossary = readGlossary();
    std::vector<juce::String> listed;
    for (const auto& [name, components] : accessibleNames (*host.editor))
    {
        CAPTURE (name, components.size());
        CHECK (name.isNotEmpty());
        CHECK (avoidedIn (name, glossary).isEmpty());
        // Titled on purpose, not by JUCE falling back to a button's text.
        for (auto* c : components)
            CHECK (c->getTitle() == name);
        listed.push_back (name);
    }
    // Every name, in order; the same on mono, as the Compact View has no Clip Light.
    const std::vector<juce::String> expected { "A/B Compare",
                                               "Close Preset browser",
                                               "Comp",
                                               "Copy A to B",
                                               "Factory",
                                               "Gain Reduction",
                                               "Gate",
                                               "Gate Threshold",
                                               "Global Bypass",
                                               juce::String (juce::CharPointer_UTF8 ("Load Preset File" "\xe2\x80\xa6")),
                                               "Next Preset",
                                               "Output Gain",
                                               "Oversampling",
                                               "Presets",
                                               "Previous Preset",
                                               "Processing Mode",
                                               "Redo",
                                               juce::String (juce::CharPointer_UTF8 ("Save as" "\xe2\x80\xa6")),
                                               "Search Presets",
                                               "Show User Presets Folder",
                                               "Style",
                                               "Switch to the Expanded View",
                                               "UI Scale",
                                               "Undo",
                                               "User" };
    CHECK (listed == expected);
}

TEST_CASE ("Tab walks the Compact View: the header, Style, Gate, Gate Threshold, Comp, Output Gain and the footer, reaching every enabled control once with a focus ring; Shift+Tab walks it back")
{
    host::EditorHost host;
    showCompactView (host);
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
        CAPTURE (c->getTitle());
        CHECK (c->isShowing());
        CHECK (c->hasFocusOutline());
        titles.add (c->getTitle());
    }
    CAPTURE (titles.joinIntoString (", "));
    // Each enabled control once; the Gain Reduction meter is no stop.
    for (auto* control : host.findAll<juce::Component> ([] (juce::Component& c) { return isControl (c) && c.isShowing() && c.isEnabled() && c.getWantsKeyboardFocus(); }))
    {
        CAPTURE (control->getTitle());
        CHECK (std::count (walked.begin(), walked.end(), control) == 1);
    }
    CHECK_FALSE (titles.contains ("Gain Reduction"));
    // In order: the header's first, then these, the body's five one after another.
    const juce::StringArray order { "Presets",       "Style",           "Gate",         "Gate Threshold", "Comp", "Output Gain",
                                    "Global Bypass", "Processing Mode", "Oversampling", "UI Scale",       "Switch to the Expanded View" };
    int last = -1;
    for (const auto& title : order)
    {
        CAPTURE (title);
        const int at = titles.indexOf (title, false, last + 1);
        CHECK (at > last);
        last = std::max (last, at);
    }
    const int style = titles.indexOf ("Style");
    CHECK (titles.indexOf ("Global Bypass") == style + 5);

    // Shift+Tab, backwards.
    std::vector<juce::Component*> back;
    for (size_t i = 0; i < walked.size(); ++i)
    {
        REQUIRE (host.press (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
        back.push_back (juce::Component::getCurrentlyFocusedComponent());
    }
    CHECK (back == std::vector<juce::Component*> (walked.rbegin(), walked.rend()));
}
