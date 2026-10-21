#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

namespace
{
// Processing Mode's plain values, in the order hosts store them (ADR 0003).
constexpr float zeroLatency = 0.0f, highQuality = 1.0f;
} // namespace

TEST_CASE ("Processing Mode is a host parameter, Zero Latency then High Quality, Zero Latency by default, not automatable")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto* mode = dynamic_cast<juce::AudioParameterChoice*> (&host::parameter (processor, "processing_mode"));
    REQUIRE (mode != nullptr);
    CHECK (mode->getName (100) == "Processing Mode");
    CHECK (mode->choices == juce::StringArray { "Zero Latency", "High Quality" });
    CHECK (mode->getIndex() == 0);
    CHECK (host::parameter (processor, "processing_mode").getDefaultValue() == 0.0f);
    CHECK_FALSE (mode->isAutomatable());
}

TEST_CASE ("Lookahead is an automatable host parameter, 0 to 5 ms, 0 ms by default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& lookahead = host::parameter (processor, "lookahead");
    CHECK (lookahead.getName (100) == "Lookahead");
    CHECK (lookahead.getLabel() == "ms");
    CHECK (lookahead.getNormalisableRange().start == 0.0f);
    CHECK (lookahead.getNormalisableRange().end == 5.0f);
    CHECK (lookahead.convertFrom0to1 (lookahead.getDefaultValue()) == 0.0f);
    CHECK (lookahead.isAutomatable());
    CHECK (lookahead.getText (lookahead.convertTo0to1 (2.5f), 100) == "2.50");
}

TEST_CASE ("The reported latency is 0 in Zero Latency and the latency query's in High Quality, at every sample rate")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const double sampleRate = GENERATE (from_range (comp1::test::sampleRates()));
    CAPTURE (sampleRate);
    processor.prepareToPlay (sampleRate, 512);
    CHECK (processor.getLatencySamples() == 0);
    host::set (processor, "processing_mode", highQuality);
    CHECK (processor.getLatencySamples() == comp1::latencySamples (comp1::ProcessingMode::HighQuality, sampleRate));
    // Lookahead moves inside the latency, never the latency itself.
    host::set (processor, "lookahead", 5.0f);
    CHECK (processor.getLatencySamples() == comp1::latencySamples (comp1::ProcessingMode::HighQuality, sampleRate));
    host::set (processor, "processing_mode", zeroLatency);
    CHECK (processor.getLatencySamples() == 0);
    // Prepared again in High Quality, at another rate, it reports that rate's.
    host::set (processor, "processing_mode", highQuality);
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == comp1::latencySamples (comp1::ProcessingMode::HighQuality, 44100.0));
}

TEST_CASE ("Restoring a session saved in High Quality reports its latency")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "processing_mode", highQuality);
    const auto state = host::saved (saved);

    comp1::PluginProcessor restored;
    restored.prepareToPlay (96000.0, 512);
    REQUIRE (restored.getLatencySamples() == 0);
    host::restore (restored, state);
    CHECK (restored.getLatencySamples() == comp1::latencySamples (comp1::ProcessingMode::HighQuality, 96000.0));
}

TEST_CASE ("Processing Mode and Lookahead survive save and load")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "processing_mode", highQuality);
    host::set (saved, "lookahead", 3.25f);
    const auto first = host::saved (saved);

    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "processing_mode") == highQuality);
    CHECK (host::value (restored, "lookahead") == host::value (saved, "lookahead"));
    CHECK (host::saved (restored) == first);
}

TEST_CASE ("In High Quality, the plugin's output is its input delayed by the reported latency")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
    host::set (processor, "ratio", 1.0f);
    host::set (processor, "processing_mode", highQuality);
    host::set (processor, "lookahead", 2.0f);
    constexpr int blockSize = 512, blocks = 8;
    processor.prepareToPlay (48000.0, blockSize);
    const int latency = processor.getLatencySamples();
    REQUIRE (latency > 0);
    const auto input = comp1::test::noise (2, blocks * blockSize, 3);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, input[static_cast<std::size_t> (ch)].data() + b * blockSize, blockSize);
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const int n = b * blockSize + i;
                REQUIRE (buffer.getSample (ch, i) == (n < latency ? 0.0f : input[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n - latency)]));
            }
    }
}
