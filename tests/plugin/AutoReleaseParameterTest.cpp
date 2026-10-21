#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

TEST_CASE ("Auto Release is an on/off host parameter, off by default, listed after Release")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& autoRelease = host::parameter (processor, "auto_release");
    CHECK (autoRelease.getName (100) == "Auto Release");
    CHECK (autoRelease.getNumSteps() == 2);
    CHECK (autoRelease.getDefaultValue() == 0.0f);
    CHECK (autoRelease.isAutomatable());
    CHECK (autoRelease.getParameterIndex() == host::parameter (processor, "release").getParameterIndex() + 1);
}

TEST_CASE ("Auto Release survives save and load, and a session saved without it restores it as off")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "auto_release", 1.0f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "auto_release") == 1.0f);
    CHECK (host::saved (restored) == first);

    // A session from before Auto Release: everything else, but no auto_release.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    auto* autoRelease = xml->getChildByAttribute ("id", "auto_release");
    REQUIRE (autoRelease != nullptr);
    xml->removeChildElement (autoRelease, true);
    host::restore (restored, *xml);
    CHECK (host::value (restored, "auto_release") == 0.0f);
}

TEST_CASE ("Auto Release set by the host reaches the Engine")
{
    juce::ScopedJuceInitialiser_GUI juce;
    // Two seconds of a steady -6 dB, compressed throughout, then a level 20 dB under Threshold: 100 ms later, with
    // Auto Release on, Release is still giving gain back, about 5 times as slowly (dyn-lab's step --auto-release), so
    // the gain is lower than with it off.
    const auto gainAfterRelease = [] (float autoRelease) {
        comp1::PluginProcessor processor;
        REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
        host::set (processor, "auto_release", autoRelease);
        constexpr int blockSize = 480;
        processor.prepareToPlay (48000.0, blockSize);
        const auto loud = comp1::test::steadyLevel (2, blockSize, -6.0);
        const auto quiet = comp1::test::steadyLevel (2, blockSize, -38.0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 210; ++b)
        {
            const auto& level = b < 200 ? loud : quiet;
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, level[static_cast<std::size_t> (ch)].data(), blockSize);
            processor.processBlock (buffer, midi);
        }
        return std::abs (buffer.getSample (0, blockSize - 1) / quiet.front().back());
    };
    CHECK (gainAfterRelease (1.0f) < gainAfterRelease (0.0f));
}
