#include "EditorHost.h"
#include "plugkit/ui/HeaderBar.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
