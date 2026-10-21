#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{
const auto mono = juce::AudioChannelSet::mono();
const auto stereo = juce::AudioChannelSet::stereo();
const auto none = juce::AudioChannelSet::disabled();

// Blocks of seeded noise on every input channel through processor, at the bus layout it has; returns the main
// output, and the main input in input.
juce::AudioBuffer<float> play (juce::AudioProcessor& processor, juce::AudioBuffer<float>& input, int blockSize = 512, int blocks = 16)
{
    processor.prepareToPlay (48000.0, blockSize);
    const int mainChannels = processor.getMainBusNumOutputChannels();
    const int channels = std::max (processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    const auto noise = comp1::test::noise (channels, blocks * blockSize, 7);
    input.setSize (mainChannels, blocks * blockSize);
    juce::AudioBuffer<float> output (mainChannels, blocks * blockSize), buffer (channels, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, noise[static_cast<std::size_t> (ch)][static_cast<std::size_t> (b * blockSize + i)]);
        for (int ch = 0; ch < mainChannels; ++ch)
            input.copyFrom (ch, b * blockSize, buffer, ch, 0, blockSize);
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < mainChannels; ++ch)
            output.copyFrom (ch, b * blockSize, buffer, ch, 0, blockSize);
    }
    return output;
}

bool sameSamples (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample (ch, i) != b.getSample (ch, i))
                return false;
    return true;
}
} // namespace

TEST_CASE ("Mono and stereo main layouts are supported, with no, a mono or a stereo Sidechain")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto main = GENERATE (mono, stereo);
    const auto sidechain = GENERATE (none, mono, stereo);
    CAPTURE (main.getDescription(), sidechain.getDescription());
    CHECK (processor.checkBusesLayoutSupported (host::layout (main, sidechain)));
}

TEST_CASE ("Other layouts are refused")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    juce::AudioProcessor::BusesLayout monoToStereo;
    monoToStereo.inputBuses.add (mono);
    monoToStereo.inputBuses.add (none);
    monoToStereo.outputBuses.add (stereo);
    CHECK_FALSE (processor.checkBusesLayoutSupported (monoToStereo));
    CHECK_FALSE (processor.checkBusesLayoutSupported (host::layout (juce::AudioChannelSet::create5point1(), none)));
    CHECK_FALSE (processor.checkBusesLayoutSupported (host::layout (none, none)));
    CHECK_FALSE (processor.checkBusesLayoutSupported (host::layout (stereo, juce::AudioChannelSet::quadraphonic())));
}

TEST_CASE ("Output Gain is a host parameter in dB, -36 to +36, defaulting to 0 dB")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& gain = host::parameter (processor, "output_gain");
    CHECK (gain.getName (100) == "Output Gain");
    CHECK (gain.getLabel() == "dB");
    CHECK (gain.getNormalisableRange().start == -36.0f);
    CHECK (gain.getNormalisableRange().end == 36.0f);
    CHECK (gain.convertFrom0to1 (gain.getDefaultValue()) == 0.0f);
    CHECK (gain.isAutomatable());
    CHECK (gain.getText (gain.convertTo0to1 (-4.5f), 100) == "-4.50");
}

TEST_CASE ("At its defaults the plugin passes the main input through bit for bit, on every supported layout")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto main = GENERATE (mono, stereo);
    const auto sidechain = GENERATE (none, mono, stereo);
    CAPTURE (main.getDescription(), sidechain.getDescription());
    REQUIRE (processor.setBusesLayout (host::layout (main, sidechain)));
    juce::AudioBuffer<float> input;
    const auto output = play (processor, input);
    CHECK (sameSamples (output, input));
}

TEST_CASE ("Output Gain set by the host applies to the main output")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto main = GENERATE (mono, stereo);
    REQUIRE (processor.setBusesLayout (host::layout (main, stereo)));
    host::set (processor, "output_gain", -18.0f); // a quarter of the way up: exactly -18 dB
    juce::AudioBuffer<float> input;
    const auto output = play (processor, input);
    constexpr double expectedGain = 0.125892541179417; // 10^(-18/20)
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        for (int i = 0; i < input.getNumSamples(); ++i)
            REQUIRE (output.getSample (ch, i) == static_cast<float> (expectedGain * input.getSample (ch, i)));
}
