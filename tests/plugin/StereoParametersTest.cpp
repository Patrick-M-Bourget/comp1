#include "Host.h"
#include "Measure.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vector>

namespace
{
const auto mono = juce::AudioChannelSet::mono();
const auto stereo = juce::AudioChannelSet::stereo();
const auto none = juce::AudioChannelSet::disabled();

// The main output after blocks of main through processor, prepared at 48 kHz: one vector per main channel.
comp1::test::Channels play (comp1::PluginProcessor& processor, const comp1::test::Channels& main, int blockSize = 512)
{
    processor.prepareToPlay (48000.0, blockSize);
    const int channels = std::max (processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    const int numSamples = static_cast<int> (main.front().size());
    comp1::test::Channels output (main.size());
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start + blockSize <= numSamples; start += blockSize)
    {
        buffer.clear();
        for (std::size_t ch = 0; ch < main.size(); ++ch)
            buffer.copyFrom (static_cast<int> (ch), 0, main[ch].data() + start, blockSize);
        processor.processBlock (buffer, midi);
        for (std::size_t ch = 0; ch < main.size(); ++ch)
            output[ch].insert (output[ch].end(), buffer.getReadPointer (static_cast<int> (ch)), buffer.getReadPointer (static_cast<int> (ch)) + blockSize);
    }
    return output;
}
} // namespace

TEST_CASE ("Stereo Placement is a host parameter, Stereo, Mid or Side, Stereo by default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& placement = host::parameter (processor, "stereo_placement");
    CHECK (placement.getName (100) == "Stereo Placement");
    CHECK (placement.getNumSteps() == 3);
    CHECK (placement.getDefaultValue() == 0.0f);
    CHECK (placement.getText (placement.convertTo0to1 (0.0f), 100) == "Stereo");
    CHECK (placement.getText (placement.convertTo0to1 (1.0f), 100) == "Mid");
    CHECK (placement.getText (placement.convertTo0to1 (2.0f), 100) == "Side");
    CHECK (placement.isAutomatable());
}

TEST_CASE ("Stereo Link is a host parameter in %, 0 to 100, 100% by default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& link = host::parameter (processor, "stereo_link");
    CHECK (link.getName (100) == "Stereo Link");
    CHECK (link.getLabel() == "%");
    CHECK (link.getNormalisableRange().start == 0.0f);
    CHECK (link.getNormalisableRange().end == 100.0f);
    CHECK (link.convertFrom0to1 (link.getDefaultValue()) == 100.0f);
    CHECK (link.isAutomatable());
    CHECK (link.getText (link.convertTo0to1 (37.5f), 100) == "37.5");
}

TEST_CASE ("Stereo Placement and Stereo Link survive save and load, and a session without them restores their defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "stereo_placement", 2.0f);
    host::set (saved, "stereo_link", 25.0f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "stereo_placement") == 2.0f);
    CHECK (host::value (restored, "stereo_link") == 25.0f);
    CHECK (host::saved (restored) == first);

    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    for (const auto* id : { "stereo_placement", "stereo_link" })
    {
        auto* setting = xml->getChildByAttribute ("id", id);
        REQUIRE (setting != nullptr);
        xml->removeChildElement (setting, true);
    }
    host::restore (restored, *xml);
    CHECK (host::value (restored, "stereo_placement") == 0.0f);
    CHECK (host::value (restored, "stereo_link") == 100.0f);
}

TEST_CASE ("Stereo Placement and Stereo Link are offered on a stereo track only")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto main = GENERATE (mono, stereo);
    const auto sidechain = GENERATE (none, mono, stereo);
    CAPTURE (main.getDescription(), sidechain.getDescription());
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (main, sidechain)));
    CHECK (processor.isStereoPlacementAvailable() == (main == stereo));
    CHECK (processor.isStereoLinkAvailable() == (main == stereo));
}

TEST_CASE ("Stereo Placement and Stereo Link set by the host reach the Engine")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    REQUIRE (processor.setBusesLayout (host::layout (stereo, none)));
    constexpr int numSamples = 512 * 16;

    // Both channels alike, loud: all Mid, so Side compresses nothing, and the output is the input.
    const auto alike = comp1::test::steadyLevel (2, numSamples, -6.0);
    host::set (processor, "stereo_placement", 2.0f);
    CHECK (play (processor, alike) == alike);

    // A loud left over a right under the Knee: unlinked, the right keeps its own Gain Reduction, none.
    const auto apart = comp1::test::steadyLevels ({ -6.0, -40.0 }, numSamples);
    host::set (processor, "stereo_placement", 0.0f);
    host::set (processor, "stereo_link", 0.0f);
    CHECK (play (processor, apart)[1] == apart[1]);
}

TEST_CASE ("A mono track is compressed as Stereo, whatever Stereo Placement and Stereo Link hold")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto input = comp1::test::noise (1, 512 * 16);
    comp1::PluginProcessor asStored, byDefault;
    for (auto* processor : { &asStored, &byDefault })
        REQUIRE (processor->setBusesLayout (host::layout (mono, none)));
    host::set (asStored, "stereo_placement", 2.0f);
    host::set (asStored, "stereo_link", 30.0f);
    CHECK (play (asStored, input) == play (byDefault, input));
}
