#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>

namespace
{
constexpr float off = -std::numeric_limits<float>::infinity();
} // namespace

TEST_CASE ("Dry and Wet are automatable host parameters in dB, Off to +12 dB, listed after Saturation and before Output Gain")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const int position = host::parameter (processor, "saturation_position").getParameterIndex();
    const auto [id, name, defaultDb, index] = GENERATE (table<const char*, const char*, float, int> ({
        { "dry", "Dry", off, 1 },
        { "wet", "Wet", 0.0f, 2 },
    }));
    CAPTURE (id);
    auto& level = host::parameter (processor, id);
    CHECK (level.getName (100) == name);
    CHECK (level.getLabel() == "dB");
    CHECK (level.isAutomatable());
    CHECK (level.getParameterIndex() == position + index);
    CHECK (host::parameter (processor, "output_gain").getParameterIndex() == position + 3);
    CHECK (level.convertFrom0to1 (level.getDefaultValue()) == defaultDb);
    // Off at the bottom of the travel, exactly, and +12 dB at the top.
    CHECK (level.convertFrom0to1 (0.0f) == off);
    CHECK (level.convertTo0to1 (off) == 0.0f);
    CHECK (level.convertFrom0to1 (1.0f) == 12.0f);
    CHECK (level.convertTo0to1 (12.0f) == 1.0f);
    // Even in dB over the audible span: equal steps of travel are equal steps in dB.
    const float step = level.convertTo0to1 (0.0f) - level.convertTo0to1 (-6.0f);
    CHECK (step > 0.0f);
    CHECK (level.convertTo0to1 (12.0f) - level.convertTo0to1 (6.0f) == Catch::Approx (step));
    CHECK (level.convertTo0to1 (-24.0f) - level.convertTo0to1 (-30.0f) == Catch::Approx (step));
    // Its text: Off at the bottom, which "Off" and "-inf" read as; dB elsewhere.
    CHECK (level.getText (0.0f, 100) == "Off");
    CHECK (level.getValueForText ("Off") == 0.0f);
    CHECK (level.getValueForText ("-inf") == 0.0f);
    CHECK (level.getText (level.convertTo0to1 (-4.5f), 100) == "-4.50");
    CHECK (level.convertFrom0to1 (level.getValueForText ("-4.50")) == Catch::Approx (-4.5f).epsilon (1.0e-5));
}

TEST_CASE ("Dry and Wet survive save and load, Off included, and a session saved without them restores the defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto [dry, wet] = GENERATE (table<float, float> ({ { -7.25f, off }, { off, 3.5f }, { 12.0f, -18.0f } }));
    CAPTURE (dry, wet);
    comp1::PluginProcessor saved;
    host::set (saved, "dry", dry);
    host::set (saved, "wet", wet);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "dry") == host::value (saved, "dry"));
    CHECK (host::value (restored, "wet") == host::value (saved, "wet"));
    CHECK ((dry == off) == (host::value (restored, "dry") == off));
    CHECK ((wet == off) == (host::value (restored, "wet") == off));
    CHECK (host::saved (restored) == first);

    // A session from before Dry and Wet.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    for (const auto* id : { "dry", "wet" })
    {
        auto* element = xml->getChildByAttribute ("id", id);
        REQUIRE (element != nullptr);
        xml->removeChildElement (element, true);
    }
    host::restore (restored, *xml);
    CHECK (host::value (restored, "dry") == off);
    CHECK (host::value (restored, "wet") == 0.0f);
}

TEST_CASE ("A host value of 0 is Off for Dry and Wet")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    for (const auto* id : { "dry", "wet" })
    {
        CAPTURE (id);
        auto& level = host::parameter (processor, id);
        level.setValueNotifyingHost (0.5f);
        level.setValueNotifyingHost (0.0f);
        CHECK (host::value (processor, id) == off);
        CHECK (level.getCurrentValueAsText() == "Off");
    }
}

TEST_CASE ("Dry and Wet set by the host reach the Engine")
{
    juce::ScopedJuceInitialiser_GUI juce;
    // At Ratio 1:1, the output is the input times the Dry and Wet levels summed, once they have glided there.
    const auto outputGain = [] (float dry, float wet) {
        comp1::PluginProcessor processor;
        REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
        host::set (processor, "ratio", 1.0f);
        host::set (processor, "dry", dry);
        host::set (processor, "wet", wet);
        constexpr int blockSize = 480;
        processor.prepareToPlay (48000.0, blockSize);
        const auto level = comp1::test::constantLevel (2, blockSize, -20.0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 10; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, level[static_cast<std::size_t> (ch)].data(), blockSize);
            processor.processBlock (buffer, midi);
        }
        return static_cast<double> (buffer.getSample (1, blockSize - 1)) / static_cast<double> (level[1].back());
    };
    CHECK (outputGain (off, 0.0f) == 1.0);
    CHECK (outputGain (0.0f, off) == 1.0);
    CHECK (outputGain (0.0f, 0.0f) == 2.0);
    // -6 dB and +6 dB, 10^(-6/20) + 10^(6/20).
    CHECK (outputGain (-6.0f, 6.0f) == Catch::Approx (0.501187233627272 + 1.99526231496888).epsilon (1.0e-5));
}
