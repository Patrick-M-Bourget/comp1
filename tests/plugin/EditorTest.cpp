#include "EditorHost.h"
#include "plugkit/ui/HeaderBar.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <map>

using Catch::Matchers::WithinAbs;

TEST_CASE ("The editor is the Expanded View, with comp1's header, on stereo, on mono and with a Sidechain")
{
    harness::OpenEditor<comp1::PluginProcessor> host;
    const auto stereo = juce::AudioChannelSet::stereo(), mono = juce::AudioChannelSet::mono();
    const auto [channels, sidechain] = GENERATE_COPY (table<juce::AudioChannelSet, juce::AudioChannelSet> ({
        { stereo, juce::AudioChannelSet::disabled() },
        { mono, juce::AudioChannelSet::disabled() },
        { stereo, stereo },
        { mono, mono },
    }));
    CAPTURE (channels.getDescription(), sidechain.getDescription());
    host.editor.reset();
    harness::useLayout (host.processor, channels, sidechain);
    host.editor.reset (host.processor.createEditor());
    host.editor->addToDesktop (0);
    host.editor->setVisible (true);

    REQUIRE (dynamic_cast<comp1::PluginEditor*> (host.editor.get()) != nullptr);
    CHECK (dynamic_cast<plugkit::ui::EditorTick::Host*> (host.editor.get()) != nullptr);
    auto* header = harness::findChild<plugkit::ui::HeaderBar> (*host.editor);
    REQUIRE (header != nullptr);
    CHECK (header->wordmark() == "comp1");
    CHECK (host.processor.hasEditor());
}

namespace
{

const auto command = juce::ModifierKeys::commandModifier;

// An edit in the editor, as a control makes it: inside a gesture.
void edit (host::EditorHost& host, const juce::String& id, float plain)
{
    auto& parameter = host.parameter (id);
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (plain));
    parameter.endChangeGesture();
}

// Keyboard focus on the component titled title, or on nothing for an empty title.
juce::Component* focusOn (host::EditorHost& host, const juce::String& title)
{
    if (title.isEmpty())
    {
        juce::Component::unfocusAllComponents();
        return nullptr;
    }
    auto& target = host.showing (title);
    target.grabKeyboardFocus();
    REQUIRE (target.hasKeyboardFocus (false));
    return &target;
}

// A click on the button, as the user makes it: its onClick at once, the button taking no keyboard focus.
void click (juce::Button& button)
{
    REQUIRE (button.onClick != nullptr);
    CHECK_FALSE (button.getMouseClickGrabsKeyboardFocus());
    button.onClick();
}

juce::Button& titled (host::EditorHost& host, const juce::String& title) { return host.showing<juce::Button> (title); }

} // namespace

TEST_CASE ("Cmd-Z undoes, Shift-Cmd-Z and Cmd-Y redo and Cmd-B toggles Global Bypass, from anywhere in the editor")
{
    host::EditorHost host;
    const auto& thresholdId = comp1::parameters::thresholdId;
    const juce::String focused = GENERATE (as<juce::String> {}, "", "Level", "Output Meter", "Presets", "Global Bypass", "UI Scale");
    CAPTURE (focused);
    edit (host, thresholdId, -30.0f);
    focusOn (host, focused);

    CHECK (host.press (juce::KeyPress ('z', command, 0)));
    CHECK_THAT (host.value (thresholdId), WithinAbs (-18.0f, 1.0e-4));
    CHECK (host.press (juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0)));
    CHECK_THAT (host.value (thresholdId), WithinAbs (-30.0f, 1.0e-4));
    CHECK (host.press (juce::KeyPress ('z', command, 0)));
    CHECK_THAT (host.value (thresholdId), WithinAbs (-18.0f, 1.0e-4));
    CHECK (host.press (juce::KeyPress ('y', command, 0)));
    CHECK_THAT (host.value (thresholdId), WithinAbs (-30.0f, 1.0e-4));

    CHECK (host.press (juce::KeyPress ('b', command, 0)));
    CHECK (host.value (comp1::parameters::globalBypassId) == 1.0f);
    CHECK (host.press (juce::KeyPress ('b', command, 0)));
    CHECK (host.value (comp1::parameters::globalBypassId) == 0.0f);
}

TEST_CASE ("The header's Undo, Redo, A/B Compare, Copy, Preset arrows and Presets button work, leaving keyboard focus where it was")
{
    host::EditorHost host;
    auto& session = host.processor.session();
    const auto& thresholdId = comp1::parameters::thresholdId;
    // Two User Presets, at -24 and -30 dB.
    edit (host, thresholdId, -24.0f);
    REQUIRE (session.presetLibrary().save ("A", session.presetState()));
    edit (host, thresholdId, -30.0f);
    REQUIRE (session.presetLibrary().save ("B", session.presetState()));
    auto* focused = focusOn (host, "Level");

    click (titled (host, "Undo"));
    CHECK_THAT (host.value (thresholdId), WithinAbs (-24.0f, 1.0e-4));
    click (titled (host, "Redo"));
    CHECK_THAT (host.value (thresholdId), WithinAbs (-30.0f, 1.0e-4));

    // B, a copy of A, edited; then copied back to A.
    click (titled (host, "A/B Compare"));
    CHECK (session.compareSide() == plugkit::ui::CompareSide::B);
    edit (host, thresholdId, -12.0f);
    host.settle();
    click (titled (host, "Copy B to A"));
    click (titled (host, "A/B Compare"));
    CHECK (session.compareSide() == plugkit::ui::CompareSide::A);
    CHECK_THAT (host.value (thresholdId), WithinAbs (-12.0f, 1.0e-4));

    // The arrows step through every Preset in browser order, the Factory's by name, Default among them, then the User
    // Presets, each loading its settings: Default's Threshold is the default, -18 dB, and A's and B's their own.
    juce::StringArray order;
    for (const auto& entry : session.presetLibrary().listing())
        order.add (entry.name);
    REQUIRE (order.size() >= 3);
    REQUIRE (order.contains ("Default"));
    REQUIRE (order.getReference (order.size() - 2) == "A");
    REQUIRE (order.getReference (order.size() - 1) == "B");
    const std::map<juce::String, float> thresholds { { "Default", -18.0f }, { "A", -24.0f }, { "B", -30.0f } };
    for (const auto& expected : order)
    {
        click (titled (host, "Next Preset"));
        REQUIRE (session.loadedPresetName() == expected);
        if (const auto threshold = thresholds.find (expected); threshold != thresholds.end())
            CHECK_THAT (host.value (thresholdId), WithinAbs (threshold->second, 1.0e-4));
    }
    click (titled (host, "Previous Preset"));
    CHECK (session.loadedPresetName() == "A");
    CHECK (focused->hasKeyboardFocus (false));

    // The Presets button opens the browser over the whole editor, listing both.
    click (titled (host, "Presets"));
    auto& browser = host.showing ("Preset browser");
    CHECK (host.inEditor (browser) == host.editor->getLocalBounds());
    auto* list = harness::findChild<juce::ListBox> (browser);
    REQUIRE (list != nullptr);
    CHECK (list->getListBoxModel()->getNumRows() == 2);
}
