#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <limits>
#include <vector>

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
        { "comp", "Comp", "%", 0.0f, 100.0f, 50.0f, 37.5f, "37.5" },
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

TEST_CASE ("The Detection EQ's Frequencies, Slopes, Gain and Q are host parameters, with their ranges and defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    // Pro-C 3's manual gives Slopes up to 96 dB/oct; the rest are comp1's own (ADR 0008).
    const auto expected = GENERATE (values<Expected> ({
        { "detection_high_pass_frequency", "Detection High-Pass Frequency", "Hz", 10.0f, 30000.0f, 20.0f, 250.0f, "250.0" },
        { "detection_high_pass_slope", "Detection High-Pass Slope", "dB/oct", 6.0f, 96.0f, 12.0f, 48.0f, "48" },
        { "detection_low_pass_frequency", "Detection Low-Pass Frequency", "Hz", 10.0f, 30000.0f, 20000.0f, 8000.0f, "8000.0" },
        { "detection_low_pass_slope", "Detection Low-Pass Slope", "dB/oct", 6.0f, 96.0f, 12.0f, 18.0f, "18" },
        { "detection_band_frequency", "Detection Free Band Frequency", "Hz", 10.0f, 30000.0f, 1000.0f, 1500.0f, "1500.0" },
        { "detection_band_gain", "Detection Free Band Gain", "dB", -30.0f, 30.0f, 0.0f, -6.5f, "-6.50" },
        { "detection_band_q", "Detection Free Band Q", "", 0.025f, 40.0f, 1.0f, 2.5f, "2.500" },
    }));
    CAPTURE (expected.id);
    auto& parameter = host::parameter (processor, expected.id);
    CHECK (parameter.getName (100) == expected.name);
    CHECK (parameter.getLabel() == expected.label);
    CHECK (parameter.getNormalisableRange().start == expected.low);
    CHECK (parameter.getNormalisableRange().end == expected.high);
    CHECK (parameter.convertFrom0to1 (0.0f) == expected.low);
    CHECK (parameter.convertFrom0to1 (1.0f) == Catch::Approx (expected.high));
    CHECK (parameter.convertFrom0to1 (parameter.getDefaultValue()) == Catch::Approx (expected.defaultValue).epsilon (1.0e-6));
    CHECK (parameter.isAutomatable());
    const float normalised = parameter.convertTo0to1 (expected.shown);
    CHECK (parameter.getText (normalised, 100) == expected.text);
    CHECK (parameter.convertFrom0to1 (parameter.getValueForText (expected.text)) == Catch::Approx (expected.shown).epsilon (1.0e-5));
}

TEST_CASE ("The Detection EQ's Frequencies and Q move evenly in log space, and its Slopes in 6 dB/oct steps")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    for (const auto* id : { "detection_high_pass_frequency", "detection_low_pass_frequency", "detection_band_frequency" })
    {
        CAPTURE (id);
        auto& frequency = host::parameter (processor, id);
        // 10 Hz to 30 kHz: each octave takes the same travel.
        CHECK (frequency.convertTo0to1 (20.0f) == Catch::Approx (std::log (2.0) / std::log (3000.0)));
        CHECK (frequency.convertTo0to1 (40.0f) - frequency.convertTo0to1 (20.0f)
               == Catch::Approx (frequency.convertTo0to1 (8000.0f) - frequency.convertTo0to1 (4000.0f)));
    }
    auto& q = host::parameter (processor, "detection_band_q");
    CHECK (q.convertTo0to1 (1.0f) == Catch::Approx (std::log (40.0) / std::log (1600.0)));
    for (const auto* id : { "detection_high_pass_slope", "detection_low_pass_slope" })
    {
        CAPTURE (id);
        auto& slope = host::parameter (processor, id);
        CHECK (slope.convertFrom0to1 (slope.convertTo0to1 (40.0f)) == 42.0f);
    }
}

TEST_CASE ("Each Detection EQ band has an on/off host parameter, off by default, and the free band a Shape")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto [id, name] = GENERATE (table<const char*, const char*> ({ { "detection_high_pass_on", "Detection High-Pass" },
                                                                          { "detection_low_pass_on", "Detection Low-Pass" },
                                                                          { "detection_band_on", "Detection Free Band" } }));
    CAPTURE (id);
    auto& on = host::parameter (processor, id);
    CHECK (on.getName (100) == name);
    CHECK (on.getNumSteps() == 2);
    CHECK (on.getDefaultValue() == 0.0f);
    CHECK (on.isAutomatable());

    auto& shape = host::parameter (processor, "detection_band_shape");
    CHECK (shape.getName (100) == "Detection Free Band Shape");
    CHECK (shape.getNumSteps() == 3);
    CHECK (shape.getDefaultValue() == 0.0f);
    CHECK (shape.getText (0.0f, 100) == "Bell");
    CHECK (shape.getText (0.5f, 100) == "Low Shelf");
    CHECK (shape.getText (1.0f, 100) == "High Shelf");
    CHECK (shape.isAutomatable());
}

TEST_CASE ("The Detection EQ's host parameters reach the Engine")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (stereo, none)));
    // Every Detection EQ setting away from its default, heard through Detection Audition.
    host::set (processor, "detection_high_pass_on", 1.0f);
    host::set (processor, "detection_high_pass_frequency", 250.0f);
    host::set (processor, "detection_high_pass_slope", 24.0f);
    host::set (processor, "detection_low_pass_on", 1.0f);
    host::set (processor, "detection_low_pass_frequency", 8000.0f);
    host::set (processor, "detection_low_pass_slope", 18.0f);
    host::set (processor, "detection_band_on", 1.0f);
    host::set (processor, "detection_band_shape", 2.0f);
    host::set (processor, "detection_band_frequency", 1500.0f);
    host::set (processor, "detection_band_gain", -6.5f);
    host::set (processor, "detection_band_q", 2.5f);
    processor.setDetectionAudition (true);
    juce::AudioBuffer<float> input;
    const auto output = play (processor, input);

    // An Engine given the same settings directly plays the same, sample for sample.
    comp1::Settings settings;
    settings.detectionAudition = true;
    auto& eq = settings.detectionEq;
    eq.highPass.on = eq.lowPass.on = eq.freeBand.on = true;
    eq.highPass.filter.frequency = host::value (processor, "detection_high_pass_frequency");
    eq.highPass.filter.slope = 24.0;
    eq.lowPass.filter.frequency = host::value (processor, "detection_low_pass_frequency");
    eq.lowPass.filter.slope = 18.0;
    eq.freeBand.filter.shape = plugkit::Shape::HighShelf;
    eq.freeBand.filter.frequency = host::value (processor, "detection_band_frequency");
    eq.freeBand.filter.gain = host::value (processor, "detection_band_gain");
    eq.freeBand.filter.q = host::value (processor, "detection_band_q");
    comp1::test::Channels expected (2);
    for (int ch = 0; ch < 2; ++ch)
        expected[static_cast<std::size_t> (ch)].assign (input.getReadPointer (ch), input.getReadPointer (ch) + input.getNumSamples());
    comp1::Engine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setSettings (settings);
    comp1::test::processInBlocks (engine, expected, { 512 });
    REQUIRE (expected[0] != std::vector<float> (input.getReadPointer (0), input.getReadPointer (0) + input.getNumSamples()));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < output.getNumSamples(); ++i)
            REQUIRE (output.getSample (ch, i) == expected[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)]);
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

TEST_CASE ("Comp Follow is an on/off host parameter, off by default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& compFollow = host::parameter (processor, "comp_follow");
    CHECK (compFollow.getName (100) == "Comp Follow");
    CHECK (compFollow.getNumSteps() == 2);
    CHECK (compFollow.getDefaultValue() == 0.0f);
    CHECK (compFollow.isAutomatable());
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

TEST_CASE ("Detection Source is a host parameter, Internal or External, Internal by default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& source = host::parameter (processor, "detection_source");
    CHECK (source.getName (100) == "Detection Source");
    CHECK (source.getNumSteps() == 2);
    CHECK (source.getDefaultValue() == 0.0f);
    CHECK (source.getText (0.0f, 100) == "Internal");
    CHECK (source.getText (1.0f, 100) == "External");
    CHECK (source.isAutomatable());
}

TEST_CASE ("With Detection Source External, the Sidechain compresses the main")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (stereo, mono)));
    // A steady -30 dB main, below Threshold, under a full-scale Sidechain, at settings that hold its peak: 18 dB of
    // Gain Reduction at Threshold -18, Ratio ∞:1 and Knee 0 (python3 tools/dyn-lab/dynlab.py transfer --threshold -18
    // --ratio inf --knee 0).
    host::set (processor, "ratio", infinity);
    host::set (processor, "knee", 0.0f);
    host::set (processor, "attack", 0.005f);
    host::set (processor, "release", 5000.0f);
    host::set (processor, "hold", 500.0f);
    host::set (processor, "detection_source", 1.0f);
    constexpr int blockSize = 512, blocks = 47;
    processor.prepareToPlay (48000.0, blockSize);
    const auto main = comp1::test::steadyLevel (2, blockSize, -30.0);
    const auto sidechainSamples = comp1::test::steadyLevel (1, blockSize, 0.0);
    juce::AudioBuffer<float> buffer (3, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, 0, main[static_cast<std::size_t> (ch)].data(), blockSize);
        buffer.copyFrom (2, 0, sidechainSamples[0].data(), blockSize);
        processor.processBlock (buffer, midi);
    }
    for (int ch = 0; ch < 2; ++ch)
        CHECK (juce::Decibels::gainToDecibels (std::abs (buffer.getSample (ch, blockSize - 1))) == Catch::Approx (-48.0).margin (0.1));
}

TEST_CASE ("Detection Audition is held through the Plugin Shell, not as a host parameter, and plays the detection signal")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (stereo, none)));
    for (auto* p : processor.getParameters())
        CHECK_FALSE (p->getName (100).containsIgnoreCase ("Audition"));
    CHECK_FALSE (processor.detectionAudition());
    host::set (processor, "threshold", -40.0f); // the noise is compressed, unless auditioned
    juce::AudioBuffer<float> input;
    REQUIRE_FALSE (sameSamples (play (processor, input), input));

    processor.setDetectionAudition (true);
    CHECK (processor.detectionAudition());
    // Held before playing starts, it plays from the first sample, with no fade: Internal's detection signal is the
    // main input.
    const auto output = play (processor, input);
    CHECK (sameSamples (output, input));
    processor.setDetectionAudition (false);
    CHECK_FALSE (processor.detectionAudition());
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
