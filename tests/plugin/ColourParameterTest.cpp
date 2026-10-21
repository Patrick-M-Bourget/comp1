#include "Host.h"
#include "ParametersOnly.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using host::ParametersOnly;

TEST_CASE ("Colour is a host parameter, 0 to 100%, 100% by default, listed after Range")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor processor;
    auto& colour = host::parameter (processor, "colour");
    CHECK (colour.getName (100) == "Colour");
    CHECK (colour.getLabel() == "%");
    CHECK (colour.getNormalisableRange().start == 0.0f);
    CHECK (colour.getNormalisableRange().end == 100.0f);
    CHECK (colour.convertFrom0to1 (colour.getDefaultValue()) == 100.0f);
    CHECK (colour.isAutomatable());
    CHECK (colour.getText (colour.convertTo0to1 (37.5f), 100) == "37.5");
    CHECK (colour.convertFrom0to1 (colour.getValueForText ("37.5")) == Catch::Approx (37.5f).epsilon (1.0e-5));
    const int range = host::parameter (processor, "range").getParameterIndex();
    CHECK (colour.getParameterIndex() == range + 1);
    CHECK (host::parameter (processor, "auto_gain").getParameterIndex() == range + 2);
}

TEST_CASE ("Colour survives save and load, and a session saved without it restores 100%")
{
    juce::ScopedJuceInitialiser_GUI juce;
    comp1::PluginProcessor saved;
    host::set (saved, "colour", 42.5f);
    const auto first = host::saved (saved);
    comp1::PluginProcessor restored;
    host::restore (restored, first);
    CHECK (host::value (restored, "colour") == host::value (saved, "colour"));
    CHECK (host::saved (restored) == first);

    // A session from before Colour.
    auto xml = host::savedXml (saved);
    REQUIRE (xml != nullptr);
    auto* element = xml->getChildByAttribute ("id", "colour");
    REQUIRE (element != nullptr);
    xml->removeChildElement (element, true);
    host::restore (restored, *xml);
    CHECK (host::value (restored, "colour") == 100.0f);
}

TEST_CASE ("Colour set by the host is the Settings' Colour")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ParametersOnly processor;
    const comp1::parameters::SettingsReader reader (processor.parameters);
    CHECK (reader.read().colour == 100.0);
    host::set (processor, "colour", 25.0f);
    CHECK (reader.read().colour == Catch::Approx (25.0).epsilon (1.0e-6));
}
