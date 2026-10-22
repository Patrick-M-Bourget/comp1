// The Factory Presets: what the plugin bundles from presets/factory, and the gate each one passes (spec #1,
// "Factory Presets"). Each rule is a function of one Preset, or of the library for unique names, giving what is wrong
// with it or nothing; the gate runs every rule over the real library, and each rule is shown to reject a bad example.

#include "ParametersOnly.h"
#include "SessionHost.h"
#include "plugkit/ui/PresetSettings.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace
{

using FactoryPreset = plugkit::ui::PresetLibrary::FactoryPreset;
namespace ids = comp1::parameters;

std::vector<FactoryPreset> factoryPresets()
{
    const host::SessionHost host;
    return host.session.presetLibrary().factoryPresets();
}

// The Factory library's names are "<Category> – <Name>", with an en dash (U+2013).
const juce::String dash = juce::String::fromUTF8 (" \xe2\x80\x93 ");
const juce::String sidechainSuffix { " (Sidechain)" };

juce::String named (const char* utf8) { return juce::String::fromUTF8 (utf8); }

// A Preset as a Factory file holds it: a comp1 root at the current state version, with a PARAM for each setting.
FactoryPreset preset (const char* nameUtf8, std::initializer_list<std::pair<const juce::String, float>> settings = {})
{
    juce::ValueTree tree ("comp1");
    tree.setProperty ("version", comp1::PluginProcessor::stateVersion, nullptr);
    for (const auto& [id, value] : settings)
        tree.appendChild (juce::ValueTree ("PARAM").setProperty ("id", id, nullptr).setProperty ("value", value, nullptr), nullptr);
    return { named (nameUtf8), tree };
}

// The value a PARAM holds, as written in the file: a number, or inf or -inf for Ratio ∞:1 and Off.
float fileValue (const juce::ValueTree& setting)
{
    const auto text = setting.getProperty ("value").toString().trim();
    if (text == "inf")
        return std::numeric_limits<float>::infinity();
    if (text == "-inf")
        return -std::numeric_limits<float>::infinity();
    return static_cast<float> (text.getDoubleValue());
}

// The PARAM the Preset holds for id, or an invalid tree.
juce::ValueTree heldSetting (const FactoryPreset& p, const juce::String& id) { return p.preset.getChildWithProperty ("id", id); }

// The Preset's settings as the Engine gets them: its PARAMs put on a fresh set of host parameters as a Preset load
// puts them, the rest at their defaults, read as the Plugin Shell reads them.
comp1::Settings settingsOf (const FactoryPreset& p)
{
    host::ParametersOnly processor;
    plugkit::ui::applyPresetSettings (processor.parameters, plugkit::ui::everyParameterButGlobalBypass, p.preset);
    return comp1::parameters::SettingsReader (processor.parameters).read();
}

juce::String categoryOf (const FactoryPreset& p) { return p.name.upToFirstOccurrenceOf (dash, false, false); }

// --- The rules -----------------------------------------------------------------------------------------------

// Rule: it loads through the Session, and restores exactly the values its file holds, every other Preset setting at
// its default.
juce::String loadingProblem (const FactoryPreset& p)
{
    host::SessionHost host;
    // Every Preset setting moved off its default first, so one the Preset leaves out has to come back to it.
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        auto& parameter = host::parameter (*host.processor, id);
        host.edit (id, parameter.convertFrom0to1 (parameter.getDefaultValue() < 0.5f ? 1.0f : 0.0f));
    }
    if (! host.session.loadPreset (p.preset, p.name))
        return "doesn't load";
    for (const auto& id : host::presetSettingIds (*host.processor))
    {
        auto& parameter = host::parameter (*host.processor, id);
        if (const auto setting = heldSetting (p, id); setting.isValid())
        {
            const auto expected = fileValue (setting);
            // As exactly as a host parameter's normalised value carries it, an infinity exactly.
            const auto restored = host.value (id);
            const bool same = std::isinf (expected) ? juce::exactlyEqual (restored, expected)
                                                    : std::abs (restored - expected) <= std::max (1.0e-4f, std::abs (expected) * 1.0e-6f);
            if (! same)
                return id + " is " + juce::String (restored) + ", not the " + juce::String (expected) + " its file holds";
        }
        else if (! juce::exactlyEqual (parameter.getValue(), parameter.getDefaultValue()))
        {
            return id + " isn't at its default";
        }
    }
    return {};
}

// Rule: it is at the current state version and holds only PARAMs, each a Preset setting away from its default: no
// unknown ID, no Global Bypass, no A/B Compare child.
juce::String formatProblem (const FactoryPreset& p)
{
    if (static_cast<int> (p.preset.getProperty ("version", 0)) != comp1::PluginProcessor::stateVersion)
        return "not at state version " + juce::String (comp1::PluginProcessor::stateVersion);
    host::SessionHost host;
    const auto settingIds = host::presetSettingIds (*host.processor);
    for (const auto& child : p.preset)
    {
        if (child.hasType (plugkit::ui::ABCompare::stateType))
            return "holds an A/B Compare child";
        if (! child.hasType ("PARAM"))
            return "holds a " + child.getType().toString();
        const auto id = child.getProperty ("id").toString();
        if (! settingIds.contains (id))
            return "holds " + id + ", not a Preset setting";
        auto& parameter = host::parameter (*host.processor, id);
        if (juce::exactlyEqual (parameter.convertFrom0to1 (parameter.getDefaultValue()), fileValue (child)))
            return id + " is at its default";
    }
    return {};
}

// Rule: no two Factory Presets share a name.
juce::String duplicateName (const std::vector<FactoryPreset>& library)
{
    std::set<juce::String> names;
    for (const auto& p : library)
        if (! names.insert (p.name).second)
            return "named twice: " + p.name;
    return {};
}

// Rule: but for Default, the name is "<Category> – <Name>", the Category one of spec #1's.
juce::String namingProblem (const FactoryPreset& p)
{
    static const juce::StringArray categories { "Vocals", "Drums", "Bass", "Guitar", "Keys", "Mix Bus", "Master", "Utility" };
    if (p.name == "Default")
        return {};
    if (! p.name.contains (dash))
        return "not named <Category>" + dash + "<Name>";
    const auto category = categoryOf (p);
    const auto name = p.name.fromFirstOccurrenceOf (dash, false, false);
    if (! categories.contains (category))
        return "no such Category: " + category;
    if (name.trim().isEmpty() || name != name.trim() || name.contains (dash))
        return "no single Name after the Category";
    return {};
}

// Rule: with Comp Follow on, it holds no Threshold, Ratio or Auto Gain, which Comp Follow ignores or holds on.
juce::String compFollowProblem (const FactoryPreset& p)
{
    if (! settingsOf (p).compFollow)
        return {};
    for (const auto& id : { ids::thresholdId, ids::ratioId, ids::autoGainId })
        if (heldSetting (p, id).isValid())
            return "holds " + id + " with Comp Follow on";
    return {};
}

bool isSidechainPreset (const FactoryPreset& p) { return p.name.endsWith (sidechainSuffix); }

// Rule: Detection Source External exactly when the name ends " (Sidechain)"; and such a Preset holds Comp Follow and
// Auto Gain off, so with nothing routed nothing lifts or lowers the signal.
juce::String sidechainProblem (const FactoryPreset& p)
{
    const auto settings = settingsOf (p);
    const bool external = settings.detectionSource == comp1::DetectionSource::External;
    if (external && ! isSidechainPreset (p))
        return "Detection Source External, without" + sidechainSuffix + " in its name";
    if (! external && isSidechainPreset (p))
        return "named" + sidechainSuffix + ", with Detection Source Internal";
    if (isSidechainPreset (p) && settings.compFollow)
        return "a Sidechain Preset with Comp Follow on";
    if (isSidechainPreset (p) && settings.autoGain)
        return "a Sidechain Preset with Auto Gain on";
    return {};
}

// Rule: High Quality only in the Master Category.
juce::String processingModeProblem (const FactoryPreset& p)
{
    if (settingsOf (p).processingMode == comp1::ProcessingMode::HighQuality && categoryOf (p) != "Master")
        return "High Quality outside Master";
    return {};
}

} // namespace

TEST_CASE ("Every file in the Factory folder is bundled as a Factory Preset, and nothing else is")
{
    // Compared by name: a Factory Preset is its file's name without the extension.
    juce::StringArray files;
    for (const auto& file : juce::File (juce::CharPointer_UTF8 (COMP1_FACTORY_PRESETS)).findChildFiles (juce::File::findFiles, false, "*.comp1preset"))
        files.add (file.getFileNameWithoutExtension());
    juce::StringArray bundled;
    for (const auto& p : factoryPresets())
        bundled.add (p.name);
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
    const auto factory = factoryPresets();
    const auto found = std::find_if (factory.begin(), factory.end(), [] (const auto& p) { return p.name == "Default"; });
    REQUIRE (found != factory.end());
    CHECK (found->preset.getNumChildren() == 0);
    CHECK (loadingProblem (*found) == "");
}

TEST_CASE ("Every Factory Preset passes the gate")
{
    const auto factory = factoryPresets();
    REQUIRE_FALSE (factory.empty());
    CHECK (duplicateName (factory) == "");
    for (const auto& p : factory)
    {
        CAPTURE (p.name);
        CHECK (loadingProblem (p) == "");
        CHECK (formatProblem (p) == "");
        CHECK (namingProblem (p) == "");
        CHECK (compFollowProblem (p) == "");
        CHECK (sidechainProblem (p) == "");
        CHECK (processingModeProblem (p) == "");
    }
}

TEST_CASE ("The Factory gate rejects a Preset that doesn't load, or doesn't restore what its file holds")
{
    CHECK (loadingProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f }, { ids::ratioId, std::numeric_limits<float>::infinity() } })) == "");
    CHECK (loadingProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -500.0f } })).isNotEmpty()); // beyond its range
    auto foreign = preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } });
    foreign.preset = juce::ValueTree ("eq1");
    CHECK (loadingProblem (foreign).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a Preset at another version, holding a default, an unknown ID or an A/B Compare child")
{
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } })) == "");
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -18.0f } })).isNotEmpty());
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { "nonsense", 1.0f } })).isNotEmpty());
    CHECK (formatProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::globalBypassId, 1.0f } })).isNotEmpty());
    auto older = preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } });
    older.preset.setProperty ("version", comp1::PluginProcessor::stateVersion - 1, nullptr);
    CHECK (formatProblem (older).isNotEmpty());
    auto compared = preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -24.0f } });
    compared.preset.appendChild (juce::ValueTree (plugkit::ui::ABCompare::stateType), nullptr);
    CHECK (formatProblem (compared).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects two Presets of one name")
{
    CHECK (duplicateName ({ preset ("Drums \xe2\x80\x93 Kick"), preset ("Drums \xe2\x80\x93 Snare") }) == "");
    CHECK (duplicateName ({ preset ("Drums \xe2\x80\x93 Kick"), preset ("Drums \xe2\x80\x93 Kick") }).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects a name outside the Category scheme")
{
    for (const auto* good : { "Default", "Vocals \xe2\x80\x93 One-Knob Lead", "Drums \xe2\x80\x93 Kick", "Bass \xe2\x80\x93 DI", "Guitar \xe2\x80\x93 Strum",
                              "Keys \xe2\x80\x93 Piano", "Mix Bus \xe2\x80\x93 Glue", "Master \xe2\x80\x93 Polish", "Utility \xe2\x80\x93 Noise Gate" })
    {
        CAPTURE (named (good));
        CHECK (namingProblem (preset (good)) == "");
    }
    for (const auto* bad : { "Kick Punch", "Drums - Kick", "Drums\xe2\x80\x93Kick", "Drum \xe2\x80\x93 Kick", "Synth \xe2\x80\x93 Lead",
                             "Drums \xe2\x80\x93  Kick", "Drums \xe2\x80\x93 ", "Drums \xe2\x80\x93 Kick \xe2\x80\x93 In" })
    {
        CAPTURE (named (bad));
        CHECK (namingProblem (preset (bad)).isNotEmpty());
    }
}

TEST_CASE ("The Factory gate rejects Threshold, Ratio or Auto Gain held with Comp Follow on")
{
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::compId, 60.0f } })) == "");
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::thresholdId, -30.0f }, { ids::autoGainId, 1.0f } })) == "");
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::thresholdId, -30.0f } })).isNotEmpty());
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::ratioId, 3.0f } })).isNotEmpty());
    CHECK (compFollowProblem (preset ("Vocals \xe2\x80\x93 Lead", { { ids::compFollowId, 1.0f }, { ids::autoGainId, 1.0f } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects Detection Source External without (Sidechain) in the name, the reverse, and a Sidechain Preset that lifts or lowers")
{
    constexpr float external = 1.0f;
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::thresholdId, -30.0f } })) == "");
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck", { { ids::detectionSourceId, external } })).isNotEmpty());
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)")).isNotEmpty());
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::compFollowId, 1.0f } })).isNotEmpty());
    CHECK (sidechainProblem (preset ("Bass \xe2\x80\x93 Duck (Sidechain)", { { ids::detectionSourceId, external }, { ids::autoGainId, 1.0f } })).isNotEmpty());
}

TEST_CASE ("The Factory gate rejects High Quality outside the Master Category")
{
    constexpr float highQuality = 1.0f;
    CHECK (processingModeProblem (preset ("Master \xe2\x80\x93 Loud", { { ids::processingModeId, highQuality } })) == "");
    CHECK (processingModeProblem (preset ("Mix Bus \xe2\x80\x93 Glue")) == "");
    CHECK (processingModeProblem (preset ("Mix Bus \xe2\x80\x93 Glue", { { ids::processingModeId, highQuality } })).isNotEmpty());
}
