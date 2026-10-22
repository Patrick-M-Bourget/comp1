#include "EditorHost.h"
#include "plugkit/ui/OutputMeter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{

const auto stereo = juce::AudioChannelSet::stereo();
const auto mono = juce::AudioChannelSet::mono();

// The editor reopened on channels, as a host opens it on that track.
void openOn (host::EditorHost& host, const juce::AudioChannelSet& channels)
{
    host.editor.reset();
    harness::useLayout (host.processor, channels);
    host.reopen();
}

// Plays a block with a sample of 1.5, an over, on channel alone, and silence elsewhere.
void playOver (host::EditorHost& host, int channel)
{
    juce::AudioBuffer<float> buffer (host.processor.getTotalNumInputChannels(), 512);
    buffer.clear();
    buffer.setSample (channel, 100, 1.5f);
    juce::MidiBuffer midi;
    host.processor.processBlock (buffer, midi);
}

plugkit::ui::OutputMeter& outputMeter (host::EditorHost& host) { return host.showing<plugkit::ui::OutputMeter> ("Output Meter"); }

// The Clip Lights' titles, as a screen reader finds them.
juce::StringArray clipLightTitles (host::EditorHost& host)
{
    juce::StringArray titles;
    for (auto* light : host.findAll<juce::Component> ([] (juce::Component& c) { return c.getTitle().startsWith ("Clip Light") && c.isVisible(); }))
        titles.add (light->getTitle());
    return titles;
}

} // namespace

TEST_CASE ("The Output Meter shows a bar and a Clip Light for each channel: one on mono, two on stereo")
{
    host::EditorHost host;
    openOn (host, mono);
    CHECK (host.processor.outputLevelChannels() == 1);
    CHECK (clipLightTitles (host) == juce::StringArray { "Clip Light" });

    openOn (host, stereo);
    CHECK (host.processor.outputLevelChannels() == 2);
    CHECK (clipLightTitles (host) == juce::StringArray { "Clip Light Left", "Clip Light Right" });
}

TEST_CASE ("An over lights its own channel's Clip Light, which stays lit until clicked, or until Space or Return on the Output Meter")
{
    host::EditorHost host;
    openOn (host, stereo);
    const int channel = GENERATE (0, 1);
    const int clear = GENERATE (0, 1, 2); // a click, Space, Return
    CAPTURE (channel, clear);

    playOver (host, channel);
    REQUIRE (host.settleUntil ([&] { return host.processor.isClipLit (channel); }));
    CHECK_FALSE (host.processor.isClipLit (1 - channel));
    // Lit, through silence.
    host.play (20, 512, [] (int) { return 0.0; });
    host.settle (200);
    CHECK (host.processor.isClipLit (channel));

    auto& meter = outputMeter (host);
    if (clear == 0)
    {
        auto& light = host.showing<juce::Component> (channel == 0 ? "Clip Light Left" : "Clip Light Right");
        meter.mouseDown (harness::mouseEvent (meter, meter.getLocalArea (&light, light.getLocalBounds()).getCentre().toFloat()));
    }
    else
    {
        meter.grabKeyboardFocus();
        CHECK (host.press (clear == 1 ? juce::KeyPress (juce::KeyPress::spaceKey) : juce::KeyPress (juce::KeyPress::returnKey)));
    }
    CHECK_FALSE (host.processor.isClipLit (0));
    CHECK_FALSE (host.processor.isClipLit (1));
}

TEST_CASE ("Restoring a session puts out the Clip Lights, which aren't saved")
{
    host::EditorHost host;
    openOn (host, stereo);
    const auto saved = host::saved (host.processor);
    playOver (host, 0);
    playOver (host, 1);
    REQUIRE (host.settleUntil ([&] { return host.processor.isClipLit (0) && host.processor.isClipLit (1); }));
    CHECK (harness::savedNamesContaining (harness::savedState (host.processor), "clip").isEmpty());

    host::restore (host.processor, saved);
    CHECK_FALSE (host.processor.isClipLit (0));
    CHECK_FALSE (host.processor.isClipLit (1));
}

TEST_CASE ("An over while the editor was closed still lights its Clip Light when the editor opens")
{
    host::EditorHost host;
    openOn (host, stereo);
    host.editor.reset();
    playOver (host, 1);
    host.play (20, 512, [] (int) { return 0.0; });
    host.reopen();
    CHECK (host.processor.isClipLit (1));
    CHECK_FALSE (host.processor.isClipLit (0));
}
