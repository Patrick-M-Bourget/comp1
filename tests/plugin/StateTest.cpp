#include "Host.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE ("Saved state carries the state version")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto xml = host::savedXml (processor);
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("version", -1) == 1);
    CHECK (comp1::PluginProcessor::stateVersion == 1);
}

TEST_CASE ("Save, load and save again gives the same bytes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "output_gain", -7.3f);
    const auto first = host::saved (saved);

    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "output_gain") == host::value (saved, "output_gain"));
    CHECK (host::saved (restored) == first);
}

TEST_CASE ("A session from a newer version loads the settings this version knows")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor source;
    host::set (source, "output_gain", 9.0f);
    auto xml = host::savedXml (source);
    REQUIRE (xml != nullptr);
    xml->setAttribute ("version", comp1::PluginProcessor::stateVersion + 1);
    auto* unknown = xml->createNewChildElement ("PARAM");
    unknown->setAttribute ("id", "from_the_future");
    unknown->setAttribute ("value", 7.0);
    xml->createNewChildElement ("SomethingNew")->setAttribute ("x", 1);

    comp1::PluginProcessor processor;
    host::restore (processor, *xml);
    CHECK (host::value (processor, "output_gain") == 9.0f);
    // Saved again, it is this version's state: the unknown parts are dropped.
    const auto resaved = host::savedXml (processor);
    CHECK (resaved->getIntAttribute ("version", -1) == comp1::PluginProcessor::stateVersion);
    CHECK (resaved->getChildByName ("SomethingNew") == nullptr);
    CHECK (resaved->getChildByAttribute ("id", "from_the_future") == nullptr);
}

TEST_CASE ("A saved value outside its range, or not a number, restores clamped, or as the default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor source;
    auto xml = host::savedXml (source);
    REQUIRE (xml != nullptr);
    auto* gain = xml->getChildByAttribute ("id", "output_gain");
    REQUIRE (gain != nullptr);

    comp1::PluginProcessor processor;
    gain->setAttribute ("value", 1000.0);
    host::restore (processor, *xml);
    CHECK (host::value (processor, "output_gain") == 36.0f);
    gain->setAttribute ("value", "nan");
    host::restore (processor, *xml);
    CHECK (host::value (processor, "output_gain") == 0.0f);
    gain->setAttribute ("value", -std::numeric_limits<double>::infinity());
    host::restore (processor, *xml);
    CHECK (host::value (processor, "output_gain") == 0.0f);
}

TEST_CASE ("A session missing a parameter restores it to its default")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    host::set (processor, "output_gain", 12.0f);
    juce::XmlElement empty ("comp1");
    empty.setAttribute ("version", 1);
    host::restore (processor, empty);
    CHECK (host::value (processor, "output_gain") == 0.0f);
}

TEST_CASE ("Anything that isn't comp1's state changes nothing")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    host::set (processor, "output_gain", 18.0f);
    juce::XmlElement other ("eq1");
    other.setAttribute ("version", 1);
    host::restore (processor, other);
    const char garbage[] = "not a state";
    processor.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
    CHECK (host::value (processor, "output_gain") == 18.0f);
}
