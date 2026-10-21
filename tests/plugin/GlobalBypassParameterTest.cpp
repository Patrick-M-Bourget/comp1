#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <functional>
#include <numbers>

namespace
{

// Processing Mode's plain values, in the order hosts store them (ADR 0003).
constexpr float zeroLatency = 0.0f, highQuality = 1.0f;

// A stereo processor with no Sidechain, compressing hard, in the Processing Mode given (a plain value).
void compressingIn (comp1::PluginProcessor& processor, float mode)
{
    REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
    host::set (processor, "threshold", -40.0f);
    host::set (processor, "ratio", 8.0f);
    host::set (processor, "processing_mode", mode);
}

// input through processor, prepared at sampleRate, in host blocks of blockSize, each played by processBlock(), or by
// processBlockBypassed() when hostBypassed (the block's first sample, in seconds) says so.
comp1::test::Channels play (comp1::PluginProcessor& processor,
                            const comp1::test::Channels& input,
                            double sampleRate,
                            const std::function<bool (double)>& hostBypassed,
                            int blockSize = 512)
{
    processor.prepareToPlay (sampleRate, blockSize);
    auto output = input;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const int length = static_cast<int> (input.front().size());
    for (int start = 0; start < length; start += blockSize)
    {
        const int count = std::min (blockSize, length - start);
        buffer.setSize (2, count, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, input[static_cast<std::size_t> (ch)].data() + start, count);
        if (hostBypassed (start / sampleRate))
            processor.processBlockBypassed (buffer, midi);
        else
            processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count, output[static_cast<std::size_t> (ch)].begin() + start);
    }
    return output;
}

} // namespace

TEST_CASE ("Global Bypass is an automatable on/off host parameter, off by default, separate from the host's bypass")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& globalBypass = host::parameter (processor, "global_bypass");
    CHECK (dynamic_cast<juce::AudioParameterBool*> (&globalBypass) != nullptr);
    CHECK (globalBypass.getName (100) == "Global Bypass");
    CHECK (globalBypass.getDefaultValue() == 0.0f);
    CHECK (globalBypass.isAutomatable());
    CHECK (processor.getBypassParameter() != &globalBypass);
}

TEST_CASE ("Global Bypass survives save and load, and a session saved without it restores it as off")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "global_bypass", 1.0f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "global_bypass") == 1.0f);
    CHECK (host::saved (restored) == first);

    // A session from before Global Bypass: everything else, but no global_bypass.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    auto* globalBypass = xml->getChildByAttribute ("id", "global_bypass");
    REQUIRE (globalBypass != nullptr);
    xml->removeChildElement (globalBypass, true);
    host::restore (restored, *xml);
    CHECK (host::value (restored, "global_bypass") == 0.0f);
}

TEST_CASE ("Global Bypass set by the host plays the input delayed by the reported latency")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const float mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (mode);
    comp1::PluginProcessor processor;
    compressingIn (processor, mode);
    const auto input = comp1::test::noise (2, 8192, 3);
    REQUIRE (play (processor, input, 48000.0, [] (double) { return false; }) != comp1::test::delayed (input, processor.getLatencySamples()));
    host::set (processor, "global_bypass", 1.0f);
    CHECK (play (processor, input, 48000.0, [] (double) { return false; }) == comp1::test::delayed (input, processor.getLatencySamples()));
}

TEST_CASE ("The host's bypass plays the input delayed by the reported latency, at every sample rate")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const float mode = GENERATE (zeroLatency, highQuality);
    const double sampleRate = GENERATE (from_range (comp1::test::sampleRates()));
    CAPTURE (mode, sampleRate);
    comp1::PluginProcessor processor;
    compressingIn (processor, mode);
    const auto input = comp1::test::noise (2, 8192, 3);
    const auto output = play (processor, input, sampleRate, [] (double) { return true; });
    const int latency = processor.getLatencySamples();
    CHECK (latency == comp1::latencySamples (mode == highQuality ? comp1::ProcessingMode::HighQuality : comp1::ProcessingMode::ZeroLatency, sampleRate));
    CHECK (output == comp1::test::delayed (input, latency));
    // The host's bypass is played through Global Bypass, never written to its host parameter.
    CHECK (host::value (processor, "global_bypass") == 0.0f);
}

TEST_CASE ("Switching between the host's bypass and processing, mid-tone, does not click")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const float mode = GENERATE (zeroLatency, highQuality);
    CAPTURE (mode);
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
    host::set (processor, "processing_mode", mode);
    host::set (processor, "output_gain", 12.0f);
    // A 200 Hz tone at -12 dB, 6 dB over the default Threshold, so the compressor is working; the host's bypass
    // switching every 50 ms, from 0.3 s on.
    constexpr double sampleRate = 48000.0, toneFrequency = 200.0, onsetSeconds = 0.3;
    comp1::test::Channels tone (2, std::vector<float> (static_cast<std::size_t> (1.5 * sampleRate)));
    for (std::size_t n = 0; n < tone.front().size(); ++n)
        tone[0][n] = tone[1][n] = static_cast<float> (0.25 * std::sin (2.0 * std::numbers::pi * toneFrequency * static_cast<double> (n) / sampleRate));
    const auto output = play (processor, tone, sampleRate, [] (double seconds) {
        return seconds >= onsetSeconds && static_cast<int> ((seconds - onsetSeconds) / 0.05) % 2 == 0;
    });
    CHECK (comp1::test::discontinuity (output, toneFrequency, sampleRate, static_cast<std::size_t> (onsetSeconds * sampleRate)) < comp1::test::clickLimit);
}
