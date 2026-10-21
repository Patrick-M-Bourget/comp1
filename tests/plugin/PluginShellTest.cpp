#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>

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

namespace
{
// A compressor host parameter as the brief and ADR 0008 give it: its ID, name, unit, range and default, and the text
// it shows for one value.
struct Expected
{
    const char* id;
    const char* name;
    const char* label;
    float low, high, defaultValue;
    float shown;
    const char* text;
};

const float infinity = std::numeric_limits<float>::infinity();
} // namespace

TEST_CASE ("The compressor's controls are host parameters, with dyn-lab's ranges and defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto expected = GENERATE (values<Expected> ({
        { "threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f, -24.5f, "-24.50" },
        { "ratio", "Ratio", ":1", 1.0f, infinity, 4.0f, 2.5f, "2.50" },
        { "knee", "Knee", "dB", 0.0f, 72.0f, 6.0f, 12.0f, "12.00" },
        { "attack", "Attack", "ms", 0.005f, 250.0f, 10.0f, 0.5f, "0.500" },
        { "release", "Release", "ms", 5.0f, 5000.0f, 100.0f, 1234.5f, "1234.5" },
        { "hold", "Hold", "ms", 0.0f, 500.0f, 0.0f, 20.0f, "20.0" },
        { "range", "Range", "dB", 0.0f, 60.0f, 60.0f, 6.0f, "6.00" },
    }));
    CAPTURE (expected.id);
    auto& parameter = host::parameter (processor, expected.id);
    CHECK (parameter.getName (100) == expected.name);
    CHECK (parameter.getLabel() == expected.label);
    CHECK (parameter.getNormalisableRange().start == expected.low);
    CHECK (parameter.getNormalisableRange().end == expected.high);
    CHECK (parameter.convertFrom0to1 (0.0f) == expected.low);
    CHECK (parameter.convertFrom0to1 (1.0f) == Catch::Approx (expected.high));
    CHECK (parameter.convertFrom0to1 (parameter.getDefaultValue()) == expected.defaultValue);
    CHECK (parameter.isAutomatable());
    const float normalised = parameter.convertTo0to1 (expected.shown);
    CHECK (parameter.getText (normalised, 100) == expected.text);
    // The text reads back as the value it shows.
    CHECK (parameter.convertFrom0to1 (parameter.getValueForText (expected.text)) == Catch::Approx (expected.shown).epsilon (1.0e-5));
}

TEST_CASE ("Ratio reaches infinity:1 at the top of its travel, and reads as the infinity sign or inf")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& ratio = host::parameter (processor, "ratio");
    CHECK (ratio.convertFrom0to1 (1.0f) == infinity);
    CHECK (ratio.convertTo0to1 (infinity) == 1.0f);
    CHECK (ratio.getText (1.0f, 100) == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")));
    CHECK (ratio.getValueForText (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) == 1.0f);
    CHECK (ratio.getValueForText ("inf") == 1.0f);
    // Its travel is finer at the low Ratios: 2:1 a quarter of the way, 4:1 a little past half.
    CHECK (ratio.convertTo0to1 (2.0f) == Catch::Approx (0.25f));
    CHECK (ratio.convertTo0to1 (4.0f) == Catch::Approx (0.5625f));
}

TEST_CASE ("Auto Gain is an on/off host parameter, off by default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& autoGain = host::parameter (processor, "auto_gain");
    CHECK (autoGain.getName (100) == "Auto Gain");
    CHECK (autoGain.getNumSteps() == 2);
    CHECK (autoGain.getDefaultValue() == 0.0f);
    CHECK (autoGain.isAutomatable());
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

TEST_CASE ("At Ratio 1:1 the plugin passes the main input through bit for bit, on every supported layout")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto main = GENERATE (mono, stereo);
    const auto sidechain = GENERATE (none, mono, stereo);
    CAPTURE (main.getDescription(), sidechain.getDescription());
    REQUIRE (processor.setBusesLayout (host::layout (main, sidechain)));
    host::set (processor, "ratio", 1.0f);
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
    host::set (processor, "ratio", 1.0f); // no Gain Reduction, so the output is the Output Gain applied
    host::set (processor, "output_gain", -18.0f); // a quarter of the way up: exactly -18 dB
    juce::AudioBuffer<float> input;
    const auto output = play (processor, input);
    constexpr double expectedGain = 0.125892541179417; // 10^(-18/20)
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        for (int i = 0; i < input.getNumSamples(); ++i)
            REQUIRE (output.getSample (ch, i) == static_cast<float> (expectedGain * input.getSample (ch, i)));
}

TEST_CASE ("The compressor's host parameters reach the Engine")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (stereo, none)));
    // A steady -6 dB, half a second of it, on both channels: dyn-lab's Transfer Curve gives -15 dB at the defaults
    // (python3 tools/dyn-lab/dynlab.py transfer) and Threshold itself, -18 dB, at Ratio ∞:1.
    const auto [ratio, expectedDb] = GENERATE (table<float, double> ({ { 4.0f, -15.0 }, { infinity, -18.0 } }));
    CAPTURE (ratio);
    host::set (processor, "ratio", ratio);
    constexpr int blockSize = 512, blocks = 47;
    processor.prepareToPlay (48000.0, blockSize);
    const auto steady = comp1::test::steadyLevel (2, blockSize, -6.0);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, steady[static_cast<std::size_t> (ch)].data(), blockSize);
        processor.processBlock (buffer, midi);
    }
    for (int ch = 0; ch < 2; ++ch)
        CHECK (juce::Decibels::gainToDecibels (std::abs (buffer.getSample (ch, blockSize - 1))) == Catch::Approx (expectedDb).margin (0.01));
}
