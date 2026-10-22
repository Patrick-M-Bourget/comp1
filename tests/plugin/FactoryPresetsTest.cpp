// The Factory Presets: what the plugin bundles from presets/factory, and the gate each one passes (spec #1,
// "Factory Presets").

#include "SessionHost.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace
{

using FactoryPreset = plugkit::ui::PresetLibrary::FactoryPreset;

std::vector<FactoryPreset> factoryPresets()
{
    const host::SessionHost host;
    return host.session.presetLibrary().factoryPresets();
}

} // namespace

TEST_CASE ("Every file in the Factory folder is bundled as a Factory Preset, and nothing else is")
{
    // Compared by name: a Factory Preset is its file's name without the extension.
    juce::StringArray files;
    for (const auto& file : juce::File (juce::CharPointer_UTF8 (COMP1_FACTORY_PRESETS)).findChildFiles (juce::File::findFiles, false, "*.comp1preset"))
        files.add (file.getFileNameWithoutExtension());
    juce::StringArray bundled;
    for (const auto& preset : factoryPresets())
        bundled.add (preset.name);
    files.sort (false);
    bundled.sort (false);
    REQUIRE (files.contains ("Default"));
    CHECK (bundled == files);

    // And the browser lists each under Factory.
    const host::SessionHost host;
    juce::StringArray listed;
    for (const auto& entry : host.session.presetLibrary().listing())
        if (entry.folder == "Factory")
            listed.add (entry.name);
    listed.sort (false);
    CHECK (listed == files);
}

TEST_CASE ("Default loads as a new instance's state: every Preset setting at its default")
{
    host::SessionHost host;
    const auto factory = factoryPresets();
    const auto found = std::find_if (factory.begin(), factory.end(), [] (const auto& preset) { return preset.name == "Default"; });
    REQUIRE (found != factory.end());
    // Every Preset setting moved to the far end of its range from its default first, so each has to come back.
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        auto& p = host::parameter (*host.processor, id);
        host.edit (id, p.convertFrom0to1 (p.getDefaultValue() < 0.5f ? 1.0f : 0.0f));
    }
    REQUIRE (host.session.loadPreset (found->preset, found->name));
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        CAPTURE (id);
        auto& p = host::parameter (*host.processor, id);
        CHECK (p.getValue() == p.getDefaultValue());
    }
}
