#include "Host.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <utility>
#include <vector>

TEST_CASE ("Saved state carries the state version")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto xml = host::savedXml (processor);
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("version", -1) == 2);
    CHECK (comp1::PluginProcessor::stateVersion == 2);
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
    // Saved again, it carries this version's number, and keeps what this version doesn't know.
    const auto resaved = host::savedXml (processor);
    CHECK (resaved->getIntAttribute ("version", -1) == comp1::PluginProcessor::stateVersion);
    CHECK (resaved->getChildByName ("SomethingNew") != nullptr);
    CHECK (resaved->getChildByAttribute ("id", "from_the_future") != nullptr);
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

TEST_CASE ("Every compressor setting survives save and load, Ratio infinity:1 included")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    const auto ratio = GENERATE (2.5f, std::numeric_limits<float>::infinity());
    CAPTURE (ratio);
    host::set (saved, "threshold", -31.5f);
    host::set (saved, "ratio", ratio);
    host::set (saved, "knee", 20.0f);
    host::set (saved, "attack", 0.5f);
    host::set (saved, "release", 50.0f);
    host::set (saved, "hold", 20.0f);
    host::set (saved, "range", 6.0f);
    host::set (saved, "auto_gain", 1.0f);
    const auto first = host::saved (saved);

    comp1::PluginProcessor restored;
    host::restore (restored, first);
    for (const auto* id : { "threshold", "ratio", "knee", "attack", "release", "hold", "range", "auto_gain" })
    {
        CAPTURE (id);
        CHECK (host::value (restored, id) == host::value (saved, id));
    }
    CHECK (host::value (restored, "ratio") == ratio);
    CHECK (host::saved (restored) == first);
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

TEST_CASE ("Detection Source survives save and load, and a session without it restores Internal")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "detection_source", 1.0f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "detection_source") == 1.0f);
    CHECK (host::saved (restored) == first);

    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    auto* source = xml->getChildByAttribute ("id", "detection_source");
    REQUIRE (source != nullptr);
    xml->removeChildElement (source, true);
    host::restore (restored, *xml);
    CHECK (host::value (restored, "detection_source") == 0.0f);
}

TEST_CASE ("Every Detection EQ setting survives save and load, and a session without them restores the defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const std::vector<std::pair<const char*, float>> moved { { "detection_high_pass_on", 1.0f },
                                                             { "detection_high_pass_frequency", 250.0f },
                                                             { "detection_high_pass_slope", 36.0f },
                                                             { "detection_low_pass_on", 1.0f },
                                                             { "detection_low_pass_frequency", 5000.0f },
                                                             { "detection_low_pass_slope", 6.0f },
                                                             { "detection_band_on", 1.0f },
                                                             { "detection_band_shape", 1.0f },
                                                             { "detection_band_frequency", 300.0f },
                                                             { "detection_band_gain", 7.5f },
                                                             { "detection_band_q", 0.5f } };
    comp1::PluginProcessor saved, defaults;
    for (const auto& [id, value] : moved)
        host::set (saved, id, value);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    for (const auto& [id, value] : moved)
    {
        CAPTURE (id);
        CHECK (host::value (restored, id) == host::value (saved, id));
        CHECK (host::value (restored, id) != host::value (defaults, id));
    }
    CHECK (host::saved (restored) == first);

    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    for (const auto& [id, value] : moved)
    {
        auto* parameter = xml->getChildByAttribute ("id", id);
        REQUIRE (parameter != nullptr);
        xml->removeChildElement (parameter, true);
    }
    host::restore (restored, *xml);
    for (const auto& [id, value] : moved)
    {
        CAPTURE (id);
        CHECK (host::value (restored, id) == host::value (defaults, id));
    }
}

TEST_CASE ("Detection Audition isn't saved, and restoring state lets go of it")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    const auto letGo = host::saved (processor);
    processor.setDetectionAudition (true);
    CHECK (host::saved (processor) == letGo);
    host::restore (processor, letGo);
    CHECK_FALSE (processor.detectionAudition());
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
