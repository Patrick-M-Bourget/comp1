#pragma once

// The Expanded View as a host opens it, through plugkit's editor harness: what the editor's tests share.

#include "Footer.h"
#include "OutputPopover.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "SessionHost.h"
#include "StereoReadout.h"
#include "plugkit/test/EditorHarness.h"
#include "plugkit/ui/staple/controls/Knob.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <utility>

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

    // The footer both views share, and its Output readout.
    comp1::Footer& footer()
    {
        auto* found = harness::findChild<comp1::Footer> (*editor);
        REQUIRE (found != nullptr);
        return *found;
    }
    plugkit::ui::OutputReadout& outputReadout() { return footer().outputReadout(); }
    // The output popover the readout opens.
    comp1::OutputPopover& outputPopover() { return footer().outputPopover(); }

    // Opens the output popover by a click on the Output readout, as the user opens it, unless it is open, and waits for
    // it to pop in.
    comp1::OutputPopover& openOutputPopover()
    {
        auto& popover = outputPopover();
        if (! popover.isOpen())
        {
            REQUIRE (outputReadout().onClick != nullptr);
            outputReadout().onClick();
        }
        REQUIRE (popover.isOpen());
        REQUIRE (settleUntil ([&popover] { return ! popover.isTransformed(); }));
        return popover;
    }

    // The footer's Stereo readout, and the stereo popover it opens by a click, as the user opens it, unless it is open,
    // waiting for it to pop in.
    comp1::StereoReadout& stereoReadout() { return footer().stereoReadout(); }
    comp1::StereoPopover& openStereoPopover()
    {
        auto& popover = stereoReadout().popover();
        if (! popover.isOpen())
        {
            REQUIRE (stereoReadout().onClick != nullptr);
            stereoReadout().onClick();
        }
        REQUIRE (popover.isOpen());
        REQUIRE (settleUntil ([&popover] { return ! popover.isTransformed(); }));
        return popover;
    }

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

    // What a block played sounds like: the processor's buffer as it leaves it, the main bus's output in its first
    // channels.
    using Heard = std::function<void (const juce::AudioBuffer<float>&)>;

    // Plays blocks of samples through the processor as the host's audio thread does: main (n) on every main channel and,
    // while a Sidechain is connected, sidechain (n) on every Sidechain channel, n the nth sample played by this call.
    // heard, when given, hears each block. Returns the main bus's last block.
    template <typename Main, typename Sidechain>
    juce::AudioBuffer<float> play (int blocks, int samples, Main&& main, Sidechain&& sidechain, const Heard& heard = {})
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumInputChannels(), samples);
        juce::MidiBuffer midi;
        for (int b = 0, n = 0; b < blocks; ++b, n += samples)
        {
            buffer.clear();
            auto mainBus = processor.getBusBuffer (buffer, true, 0);
            for (int ch = 0; ch < mainBus.getNumChannels(); ++ch)
                for (int i = 0; i < samples; ++i)
                    mainBus.setSample (ch, i, static_cast<float> (main (n + i)));
            if (processor.getBusCount (true) > 1 && processor.getBus (true, 1)->isEnabled())
            {
                auto sidechainBus = processor.getBusBuffer (buffer, true, 1);
                for (int ch = 0; ch < sidechainBus.getNumChannels(); ++ch)
                    for (int i = 0; i < samples; ++i)
                        sidechainBus.setSample (ch, i, static_cast<float> (sidechain (n + i)));
            }
            processor.processBlock (buffer, midi);
            if (heard != nullptr)
                heard (buffer);
        }
        juce::AudioBuffer<float> out (processor.getMainBusNumOutputChannels(), samples);
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.copyFrom (ch, 0, buffer, ch, 0, samples);
        return out;
    }

    // The same with the Sidechain silent.
    template <typename Main>
    juce::AudioBuffer<float> play (int blocks, int samples, Main&& main)
    {
        return play (blocks, samples, std::forward<Main> (main), silent);
    }

    // A silent signal, for the Sidechain or the main input.
    static double silent (int) { return 0.0; }
};

} // namespace host
