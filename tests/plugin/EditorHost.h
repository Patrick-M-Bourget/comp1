#pragma once

// The Expanded View as a host opens it, through plugkit's editor harness: what the editor's tests share.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "SessionHost.h"
#include "plugkit/test/EditorHarness.h"
#include "plugkit/ui/staple/controls/Knob.h"

#include <catch2/catch_test_macros.hpp>

namespace host
{

// A folder of its own, built before the processor that keeps its files there, and deleted after it.
struct TemporaryFiles
{
    TemporaryFolder temporary;
};

// The plugin with its per-user settings and User Presets in a temporary folder (TemporaryFolder): what a test that
// picks a UI Scale or saves a Preset opens, so it never writes the user's own.
struct TemporaryPluginProcessor : private TemporaryFiles, public comp1::PluginProcessor
{
    TemporaryPluginProcessor()
        : comp1::PluginProcessor (temporary.folder.getChildFile ("comp1.settings"), temporary.folder.getChildFile ("Presets"))
    {
    }
};

// The editor open in a window of its own, on a processor whose files are in a temporary folder.
struct EditorHost : harness::OpenEditor<TemporaryPluginProcessor>
{
    comp1::PluginEditor& expandedView() { return dynamic_cast<comp1::PluginEditor&> (*editor); }

    // Closes the editor and opens a new one, in a window of its own, as a host does.
    void reopen()
    {
        juce::PopupMenu::dismissAllActiveMenus();
        editor.reset();
        editor.reset (processor.createEditor());
        editor->addToDesktop (0);
        editor->setVisible (true);
    }

    // An edit as a control makes it: inside a gesture.
    void edit (const juce::String& id, float plain)
    {
        auto& p = parameter (id);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    }

    // The knob titled title, showing.
    staple::Knob& knob (const juce::String& title) { return showing<staple::Knob> (title); }

    // A mouse press on knob's face, a drag pixels up from it (down when negative), and the release, through the
    // knob's own handlers (CODING_STANDARDS "Editor behaviour").
    static void drag (staple::Knob& knob, float pixels)
    {
        const auto mods = juce::ModifierKeys::leftButtonModifier;
        const auto centre = knob.getFaceCentre();
        knob.mouseDown (harness::mouseEvent (knob, centre, mods));
        if (! juce::exactlyEqual (pixels, 0.0f))
            knob.mouseDrag (harness::mouseEvent (knob, centre.translated (0.0f, -pixels), mods, centre));
        knob.mouseUp (harness::mouseEvent (knob, centre.translated (0.0f, -pixels), {}, centre));
    }

    // A click on button, as the user makes it: what JUCE's click does, at once (a test window isn't in front, so the
    // pointer's own events don't reach it).
    static void click (juce::Button& button)
    {
        if (button.getClickingTogglesState())
            button.setToggleState (! button.getToggleState(), juce::sendNotificationSync);
        else
        {
            REQUIRE (button.onClick != nullptr);
            button.onClick();
        }
    }

    // A pick from box's menu, as the user makes it.
    static void pick (juce::ComboBox& box, const juce::String& item)
    {
        for (int i = 0; i < box.getNumItems(); ++i)
            if (box.getItemText (i) == item)
            {
                box.setSelectedItemIndex (i, juce::sendNotificationSync);
                return;
            }
        FAIL (box.getTitle() << " doesn't offer " << item);
    }

    // Plays blocks of samples of signal through the processor as the host's audio thread does, on every main
    // channel: signal (n) is the nth sample played by this call.
    template <typename Signal>
    void play (int blocks, int samples, Signal&& signal)
    {
        const int channels = processor.getTotalNumInputChannels();
        juce::AudioBuffer<float> buffer (channels, samples);
        juce::MidiBuffer midi;
        for (int b = 0, n = 0; b < blocks; ++b, n += samples)
        {
            buffer.clear();
            for (int ch = 0; ch < processor.getMainBusNumInputChannels(); ++ch)
                for (int i = 0; i < samples; ++i)
                    buffer.setSample (ch, i, static_cast<float> (signal (n + i)));
            processor.processBlock (buffer, midi);
        }
    }
};

} // namespace host
