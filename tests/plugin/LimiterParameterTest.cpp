#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE ("The Limiter and its Ceiling are host parameters, listed after Output Gain")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const int outputGain = host::parameter (processor, "output_gain").getParameterIndex();

    auto& limiter = host::parameter (processor, "limiter");
    CHECK (limiter.getName (100) == "Limiter");
    CHECK (limiter.getNumSteps() == 2);
    CHECK (limiter.getDefaultValue() == 0.0f);
    CHECK (limiter.isAutomatable());
    CHECK (limiter.getParameterIndex() == outputGain + 1);

    auto& ceiling = host::parameter (processor, "ceiling");
    CHECK (ceiling.getName (100) == "Ceiling");
    CHECK (ceiling.getLabel() == "dBFS");
    CHECK (ceiling.getNormalisableRange().start == -30.0f);
    CHECK (ceiling.getNormalisableRange().end == 0.0f);
    CHECK (ceiling.convertFrom0to1 (ceiling.getDefaultValue()) == 0.0f);
    CHECK (ceiling.isAutomatable());
    CHECK (ceiling.getText (ceiling.convertTo0to1 (-1.5f), 100) == "-1.50");
    CHECK (ceiling.convertFrom0to1 (ceiling.getValueForText ("-1.50")) == Catch::Approx (-1.5f).epsilon (1.0e-5));
    // Linear in dB: halfway is -15 dBFS.
    CHECK (ceiling.convertFrom0to1 (0.5f) == -15.0f);
    CHECK (ceiling.getParameterIndex() == outputGain + 2);
}

TEST_CASE ("The Limiter's settings survive save and load, and a session saved without them restores the defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "limiter", 1.0f);
    host::set (saved, "ceiling", -7.25f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    for (const auto* id : { "limiter", "ceiling" })
    {
        CAPTURE (id);
        CHECK (host::value (restored, id) == host::value (saved, id));
    }
    CHECK (host::value (restored, "ceiling") == -7.25f);
    CHECK (host::saved (restored) == first);

    // A session from before the Limiter: everything else, but neither of its two.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    for (const auto* id : { "limiter", "ceiling" })
    {
        auto* element = xml->getChildByAttribute ("id", id);
        REQUIRE (element != nullptr);
        xml->removeChildElement (element, true);
    }
    host::restore (restored, *xml);
    CHECK (host::value (restored, "limiter") == 0.0f);
    CHECK (host::value (restored, "ceiling") == 0.0f);
}

TEST_CASE ("The Limiter set by the host reaches the Engine, at the Ceiling the host sets")
{
    juce::ScopedJuceInitialiser_GUI juce;
    // A steady 0 dBFS at Ratio 1:1: with the Limiter on at a Ceiling of -6 dBFS, Zero Latency's brings it to -6 dBFS.
    const auto outputDb = [] (float on) {
        comp1::PluginProcessor processor;
        REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
        host::set (processor, "ratio", 1.0f);
        host::set (processor, "limiter", on);
        host::set (processor, "ceiling", -6.0f);
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
    CHECK (std::abs (outputDb (0.0f)) <= 0.001);
    CHECK (std::abs (outputDb (1.0f) - -6.0) <= 0.001);
}
