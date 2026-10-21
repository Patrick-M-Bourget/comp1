#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE ("Saturation, Drive and Saturation Position are host parameters, listed after Auto Gain")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const int autoGain = host::parameter (processor, "auto_gain").getParameterIndex();

    auto& saturation = host::parameter (processor, "saturation");
    CHECK (saturation.getName (100) == "Saturation");
    CHECK (saturation.getNumSteps() == 2);
    CHECK (saturation.getDefaultValue() == 0.0f);
    CHECK (saturation.isAutomatable());
    CHECK (saturation.getParameterIndex() == autoGain + 1);

    auto& drive = host::parameter (processor, "drive");
    CHECK (drive.getName (100) == "Drive");
    CHECK (drive.getLabel() == "dB");
    CHECK (drive.getNormalisableRange().start == 0.0f);
    CHECK (drive.getNormalisableRange().end == 24.0f);
    CHECK (drive.convertFrom0to1 (drive.getDefaultValue()) == 6.0f);
    CHECK (drive.isAutomatable());
    CHECK (drive.getText (drive.convertTo0to1 (12.5f), 100) == "12.50");
    CHECK (drive.convertFrom0to1 (drive.getValueForText ("12.50")) == Catch::Approx (12.5f).epsilon (1.0e-5));
    CHECK (drive.getParameterIndex() == autoGain + 2);

    // Before, then After, frozen in that order (ADR 0003); After by default.
    auto& position = host::parameter (processor, "saturation_position");
    CHECK (position.getName (100) == "Saturation Position");
    CHECK (position.getNumSteps() == 2);
    CHECK (position.getText (0.0f, 100) == "Before");
    CHECK (position.getText (1.0f, 100) == "After");
    CHECK (position.getDefaultValue() == 1.0f);
    CHECK (position.isAutomatable());
    CHECK (position.getParameterIndex() == autoGain + 3);
    CHECK (host::parameter (processor, "output_gain").getParameterIndex() == autoGain + 4);
}

TEST_CASE ("Saturation's settings survive save and load, and a session saved without them restores the defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "saturation", 1.0f);
    host::set (saved, "drive", 17.25f);
    host::set (saved, "saturation_position", 0.0f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    for (const auto* id : { "saturation", "drive", "saturation_position" })
    {
        CAPTURE (id);
        CHECK (host::value (restored, id) == host::value (saved, id));
    }
    CHECK (host::saved (restored) == first);

    // A session from before Saturation: everything else, but none of its three.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    for (const auto* id : { "saturation", "drive", "saturation_position" })
    {
        auto* element = xml->getChildByAttribute ("id", id);
        REQUIRE (element != nullptr);
        xml->removeChildElement (element, true);
    }
    host::restore (restored, *xml);
    CHECK (host::value (restored, "saturation") == 0.0f);
    CHECK (host::value (restored, "drive") == 6.0f);
    CHECK (host::value (restored, "saturation_position") == 1.0f);
}

TEST_CASE ("Saturation set by the host reaches the Engine, at the Drive and Saturation Position the host sets")
{
    juce::ScopedJuceInitialiser_GUI juce;
    // A steady 0 dBFS at Ratio 1:1 through Saturation at Drive 12 dB comes out at -6.00 dB (python3
    // tools/dyn-lab/dynlab.py saturation --drive 12), at either Saturation Position.
    const auto outputDb = [] (float on, float position) {
        comp1::PluginProcessor processor;
        REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
        host::set (processor, "ratio", 1.0f);
        host::set (processor, "saturation", on);
        host::set (processor, "drive", 12.0f);
        host::set (processor, "saturation_position", position);
        constexpr int blockSize = 480;
        processor.prepareToPlay (48000.0, blockSize);
        const auto level = comp1::test::constantLevel (2, blockSize, 0.0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 10; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, level[static_cast<std::size_t> (ch)].data(), blockSize);
            processor.processBlock (buffer, midi);
        }
        return 20.0 * std::log10 (static_cast<double> (buffer.getSample (1, blockSize - 1)));
    };
    CHECK (std::abs (outputDb (0.0f, 1.0f)) <= 0.001);
    CHECK (std::abs (outputDb (1.0f, 1.0f) - -6.00) <= 0.01);
    CHECK (std::abs (outputDb (1.0f, 0.0f) - -6.00) <= 0.01);
}
