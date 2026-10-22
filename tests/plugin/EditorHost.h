#pragma once

// The Expanded View as a host opens it, through plugkit's editor harness: what the editor's tests share.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "SessionHost.h"
#include "plugkit/test/EditorHarness.h"

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
