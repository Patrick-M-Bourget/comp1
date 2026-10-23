#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace
{

// The Gate's host parameters, in the order hosts list them.
constexpr const char* gateIds[] = { "gate", "gate_threshold", "gate_range", "gate_attack", "gate_release", "gate_hold", "gate_position" };

} // namespace

TEST_CASE ("The Gate's seven host parameters are listed after Stereo Link and before the compressor's, Comp first, with the compressor's ranges")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const int stereoLink = host::parameter (processor, "stereo_link").getParameterIndex();
    for (int i = 0; i < 7; ++i)
    {
        CAPTURE (gateIds[i]);
        CHECK (host::parameter (processor, gateIds[i]).getParameterIndex() == stereoLink + 1 + i);
        CHECK (host::parameter (processor, gateIds[i]).isAutomatable());
    }
    CHECK (host::parameter (processor, "comp").getParameterIndex() == stereoLink + 8);

    auto& gate = host::parameter (processor, "gate");
    CHECK (gate.getName (100) == "Gate");
    CHECK (gate.getNumSteps() == 2);
    CHECK (gate.getDefaultValue() == 0.0f);

    auto& threshold = host::parameter (processor, "gate_threshold");
    CHECK (threshold.getName (100) == "Gate Threshold");
    CHECK (threshold.getLabel() == "dB");
    CHECK (threshold.getNormalisableRange().start == -60.0f);
    CHECK (threshold.getNormalisableRange().end == 0.0f);
    CHECK (threshold.convertFrom0to1 (threshold.getDefaultValue()) == -40.0f);
    // Linear: halfway is -30 dB.
    CHECK (threshold.convertFrom0to1 (0.5f) == -30.0f);

    auto& range = host::parameter (processor, "gate_range");
    CHECK (range.getName (100) == "Gate Range");
    CHECK (range.getLabel() == "dB");
    CHECK (range.getNormalisableRange().start == 0.0f);
    CHECK (range.getNormalisableRange().end == 60.0f);
    CHECK (range.convertFrom0to1 (range.getDefaultValue()) == 20.0f);
    CHECK (range.convertFrom0to1 (0.5f) == 30.0f);

    // Log travel: each doubling takes the same travel, so the range's geometric middle sits halfway.
    auto& attack = host::parameter (processor, "gate_attack");
    CHECK (attack.getName (100) == "Gate Attack");
    CHECK (attack.getLabel() == "ms");
    CHECK (attack.getNormalisableRange().start == 0.005f);
    CHECK (attack.getNormalisableRange().end == 250.0f);
    CHECK (attack.convertFrom0to1 (attack.getDefaultValue()) == Catch::Approx (1.0f).epsilon (1.0e-6));
    CHECK (attack.convertFrom0to1 (0.5f) == Catch::Approx (std::sqrt (0.005 * 250.0)).epsilon (1.0e-5));

    auto& release = host::parameter (processor, "gate_release");
    CHECK (release.getName (100) == "Gate Release");
    CHECK (release.getLabel() == "ms");
    CHECK (release.getNormalisableRange().start == 5.0f);
    CHECK (release.getNormalisableRange().end == 5000.0f);
    CHECK (release.convertFrom0to1 (release.getDefaultValue()) == Catch::Approx (100.0f).epsilon (1.0e-6));
    CHECK (release.convertFrom0to1 (0.5f) == Catch::Approx (std::sqrt (5.0 * 5000.0)).epsilon (1.0e-5));

    // Hold's travel: 50 ms at the centre.
    auto& hold = host::parameter (processor, "gate_hold");
    CHECK (hold.getName (100) == "Gate Hold");
    CHECK (hold.getLabel() == "ms");
    CHECK (hold.getNormalisableRange().start == 0.0f);
    CHECK (hold.getNormalisableRange().end == 500.0f);
    CHECK (hold.convertFrom0to1 (hold.getDefaultValue()) == Catch::Approx (20.0f).epsilon (1.0e-5));
    CHECK (hold.convertFrom0to1 (0.5f) == Catch::Approx (50.0f).epsilon (1.0e-5));
    CHECK (hold.convertFrom0to1 (0.5f) == Catch::Approx (host::parameter (processor, "hold").convertFrom0to1 (0.5f)));

    // Pre, then Post, frozen in that order (ADR 0003); Pre by default.
    auto& position = host::parameter (processor, "gate_position");
    CHECK (position.getName (100) == "Gate Position");
    CHECK (position.getNumSteps() == 2);
    CHECK (position.getText (0.0f, 100) == "Pre");
    CHECK (position.getText (1.0f, 100) == "Post");
    CHECK (position.getDefaultValue() == 0.0f);
}

TEST_CASE ("The Gate's settings survive save and load, and a session saved without them restores the defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "gate", 1.0f);
    host::set (saved, "gate_threshold", -52.5f);
    host::set (saved, "gate_range", 33.25f);
    host::set (saved, "gate_attack", 4.0f);
    host::set (saved, "gate_release", 640.0f);
    host::set (saved, "gate_hold", 125.0f);
    host::set (saved, "gate_position", 1.0f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    for (const auto* id : gateIds)
    {
        CAPTURE (id);
        CHECK (host::value (restored, id) == host::value (saved, id));
    }
    CHECK (host::value (restored, "gate_threshold") == -52.5f);
    CHECK (host::saved (restored) == first);

    // A session from before the Gate: everything else, but none of its seven.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    for (const auto* id : gateIds)
    {
        auto* element = xml->getChildByAttribute ("id", id);
        REQUIRE (element != nullptr);
        xml->removeChildElement (element, true);
    }
    host::restore (restored, *xml);
    const comp1::Settings::Gate defaults;
    CHECK (host::value (restored, "gate") == 0.0f);
    CHECK (host::value (restored, "gate_threshold") == static_cast<float> (defaults.thresholdDb));
    CHECK (host::value (restored, "gate_range") == static_cast<float> (defaults.rangeDb));
    CHECK (host::value (restored, "gate_attack") == Catch::Approx (defaults.attackMs).epsilon (1.0e-6));
    CHECK (host::value (restored, "gate_release") == Catch::Approx (defaults.releaseMs).epsilon (1.0e-6));
    CHECK (host::value (restored, "gate_hold") == Catch::Approx (defaults.holdMs).epsilon (1.0e-5));
    CHECK (host::value (restored, "gate_position") == 0.0f);
}

TEST_CASE ("The Gate set by the host reaches the Engine")
{
    juce::ScopedJuceInitialiser_GUI juce;
    // A steady -40 dB at Ratio 1:1: with the Gate on at Gate Threshold -20 dB and Gate Range 30 dB, Gate Release
    // 5 ms and Gate Hold 0 ms, it closes, and the output is 30 dB down.
    const auto outputDb = [] (float on) {
        comp1::PluginProcessor processor;
        REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
        host::set (processor, "ratio", 1.0f);
        host::set (processor, "gate", on);
        host::set (processor, "gate_threshold", -20.0f);
        host::set (processor, "gate_range", 30.0f);
        host::set (processor, "gate_attack", 1.0f);
        host::set (processor, "gate_release", 5.0f);
        host::set (processor, "gate_hold", 0.0f);
        constexpr int blockSize = 480;
        processor.prepareToPlay (48000.0, blockSize);
        const auto level = comp1::test::steadyLevel (2, blockSize, -40.0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 20; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, level[static_cast<std::size_t> (ch)].data(), blockSize);
            processor.processBlock (buffer, midi);
        }
        return 20.0 * std::log10 (std::abs (static_cast<double> (buffer.getSample (1, blockSize - 1))));
    };
    CHECK (std::abs (outputDb (0.0f) - -40.0) <= 0.001);
    CHECK (std::abs (outputDb (1.0f) - -70.0) <= 0.001);
}

TEST_CASE ("Gate Position set by the host reaches the Engine: at Post the Gate leaves the Dry path alone")
{
    juce::ScopedJuceInitialiser_GUI juce;
    // A steady -40 dB at Dry 0 dB and Wet Off, the Gate on at Gate Threshold -20 dB and Gate Range 30 dB: at Pre it
    // closes on the Dry path too, 30 dB down; at Post the Dry path passes as it came in.
    const auto outputDb = [] (float position) {
        comp1::PluginProcessor processor;
        REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
        host::set (processor, "gate", 1.0f);
        host::set (processor, "gate_threshold", -20.0f);
        host::set (processor, "gate_range", 30.0f);
        host::set (processor, "gate_release", 5.0f);
        host::set (processor, "gate_hold", 0.0f);
        host::set (processor, "gate_position", position);
        host::set (processor, "dry", 0.0f);
        host::set (processor, "wet", -std::numeric_limits<float>::infinity());
        constexpr int blockSize = 480;
        processor.prepareToPlay (48000.0, blockSize);
        const auto level = comp1::test::steadyLevel (2, blockSize, -40.0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 20; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, level[static_cast<std::size_t> (ch)].data(), blockSize);
            processor.processBlock (buffer, midi);
        }
        return 20.0 * std::log10 (std::abs (static_cast<double> (buffer.getSample (1, blockSize - 1))));
    };
    CHECK (std::abs (outputDb (0.0f) - -70.0) <= 0.001);
    CHECK (std::abs (outputDb (1.0f) - -40.0) <= 0.001);
}
