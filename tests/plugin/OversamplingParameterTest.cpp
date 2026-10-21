#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{
// Oversampling's plain values, in the order hosts store them (ADR 0003).
constexpr float x2 = 1.0f, x8 = 3.0f;
} // namespace

TEST_CASE ("Oversampling is a host parameter, 1x, 2x, 4x then 8x, 2x by default, not automatable")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto* oversampling = dynamic_cast<juce::AudioParameterChoice*> (&host::parameter (processor, "oversampling"));
    REQUIRE (oversampling != nullptr);
    CHECK (oversampling->getName (100) == "Oversampling");
    CHECK (oversampling->choices == juce::StringArray { "1x", "2x", "4x", "8x" });
    CHECK (oversampling->getIndex() == 1);
    CHECK (host::value (processor, "oversampling") == x2);
    CHECK_FALSE (oversampling->isAutomatable());
}

TEST_CASE ("A stored Oversampling round-trips unchanged through save and load, at any rate, its cap included")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const double sampleRate = GENERATE (from_range (comp1::test::sampleRates()));
    CAPTURE (sampleRate);
    comp1::PluginProcessor saved;
    saved.prepareToPlay (sampleRate, 2048);
    host::set (saved, "oversampling", x8);
    const auto state = host::saved (saved);

    comp1::PluginProcessor restored;
    restored.prepareToPlay (sampleRate, 2048);
    host::restore (restored, state);
    // 8x is over the cap at 88.2 kHz and up, and plays the cap there, but the setting is kept as it was.
    CHECK (host::value (restored, "oversampling") == x8);
    CHECK (host::saved (restored) == state);
}

TEST_CASE ("Oversampling reaches the Engine, and never moves the reported latency")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
    host::set (processor, "processing_mode", 1.0f);
    host::set (processor, "saturation", 1.0f);
    host::set (processor, "drive", 24.0f);
    processor.prepareToPlay (44100.0, 2048);
    const int latency = processor.getLatencySamples();
    const auto play = [&] (float oversampling) {
        host::set (processor, "oversampling", oversampling);
        processor.prepareToPlay (44100.0, 2048);
        CHECK (processor.getLatencySamples() == latency);
        auto input = comp1::test::noise (2, 2048, 3);
        juce::AudioBuffer<float> buffer (2, 2048);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, input[static_cast<std::size_t> (ch)].data(), 2048);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        return std::vector<float> (buffer.getReadPointer (0), buffer.getReadPointer (0) + 2048);
    };
    CHECK (play (0.0f) != play (x8));
}
