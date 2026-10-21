// User Presets, A/B Compare and undo and redo, through the Plugin Shell's Session, as the editor will drive them.

#include "Host.h"
#include "PluginProcessor.h"
#include "comp1/Latency.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>
#include <memory>

using Catch::Matchers::WithinAbs;
using plugkit::ui::CompareSide;

namespace
{
constexpr float zeroLatency = 0.0f, highQuality = 1.0f;
constexpr float infinity = std::numeric_limits<float>::infinity();

// A folder of its own for each test's per-user settings and User Presets, deleted after it, so no test reaches the
// real Documents folder or settings file. Named by a UUID, as tests run in parallel processes.
struct TemporaryFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("comp1-session-test-" + juce::Uuid().toString());
    TemporaryFolder() { REQUIRE (folder.createDirectory()); }
    ~TemporaryFolder() { folder.deleteRecursively(); }
};

// The plugin as a host has it, its session kept in a temporary folder.
struct Host
{
    juce::ScopedJuceInitialiser_GUI juce;
    TemporaryFolder temporary;
    std::unique_ptr<comp1::PluginProcessor> processor = make();
    plugkit::ui::Session& session = processor->session();
    plugkit::ui::EditHistory& history = session.editHistory();

    std::unique_ptr<comp1::PluginProcessor> make() const
    {
        return std::make_unique<comp1::PluginProcessor> (temporary.folder.getChildFile ("comp1.settings"),
                                                         temporary.folder.getChildFile ("Presets"));
    }

    float value (const juce::String& id) const { return host::value (*processor, id); }

    // An edit in the editor: inside a gesture.
    void edit (const juce::String& id, float plain) const
    {
        auto& p = host::parameter (*processor, id);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
        p.endChangeGesture();
    }
};

// The parameters a Preset and each A/B side hold: every host parameter but Global Bypass.
juce::StringArray presetSettingIds (juce::AudioProcessor& processor)
{
    juce::StringArray ids;
    for (auto* p : processor.getParameters())
        if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (p);
            ranged != nullptr && ranged->getParameterID() != plugkit::ui::globalBypassId)
            ids.add (ranged->getParameterID());
    return ids;
}
} // namespace

TEST_CASE ("A version 1 session restores on side A, with B a copy and no Loaded Preset, and reports its latency")
{
    // As main saved it before A/B Compare: the parameters' state and version 1, nothing else.
    Host source;
    host::set (*source.processor, "ratio", infinity);
    host::set (*source.processor, "threshold", -27.0f);
    host::set (*source.processor, "processing_mode", highQuality);
    auto xml = host::savedXml (*source.processor);
    REQUIRE (xml != nullptr);
    xml->deleteAllChildElementsWithTagName (plugkit::ui::ABCompare::stateType.toString());
    for (const auto* property : { "editorWidth", "editorHeight", "uiScalePercent" })
        xml->removeAttribute (property);
    xml->setAttribute ("version", 1);

    Host host;
    host.processor->prepareToPlay (96000.0, 512);
    host::restore (*host.processor, *xml);
    CHECK (host.session.compareSide() == CompareSide::A);
    CHECK (host.session.loadedPresetName().isEmpty());
    for (const auto& id : presetSettingIds (*host.processor))
    {
        CAPTURE (id);
        CHECK (host.value (id) == source.value (id));
    }
    CHECK (host.processor->getLatencySamples() == comp1::latencySamples (comp1::ProcessingMode::HighQuality, 96000.0));

    host.session.selectCompareSide (CompareSide::B);
    CHECK (host.session.loadedPresetName().isEmpty());
    for (const auto& id : presetSettingIds (*host.processor))
    {
        CAPTURE (id);
        CHECK (host.value (id) == source.value (id));
    }
}

TEST_CASE ("A Preset holds every host parameter as plain values, Processing Mode included, and nothing of the window")
{
    Host host;
    host.edit ("ratio", infinity);
    host.edit ("output_gain", -4.5f);
    host.edit ("processing_mode", highQuality);
    host.edit ("lookahead", 2.0f);
    const auto preset = host.session.presetState();
    CHECK (preset.hasType ("comp1"));
    CHECK (static_cast<int> (preset.getProperty ("version")) == comp1::PluginProcessor::stateVersion);
    CHECK (comp1::PluginProcessor::stateVersion == 2);
    for (const auto* property : { "editorWidth", "editorHeight", "uiScalePercent" })
        CHECK_FALSE (preset.hasProperty (property));
    CHECK_FALSE (preset.getChildWithName (plugkit::ui::ABCompare::stateType).isValid());
    for (const auto& id : presetSettingIds (*host.processor))
    {
        CAPTURE (id);
        const auto setting = preset.getChildWithProperty ("id", id);
        REQUIRE (setting.isValid());
        CHECK (static_cast<float> (setting.getProperty ("value")) == host.value (id));
    }
    CHECK (static_cast<float> (preset.getChildWithProperty ("id", "processing_mode").getProperty ("value")) == highQuality);
}

TEST_CASE ("A Preset saved as a .comp1preset file loads back every setting, Ratio infinity:1 included, as one undo step")
{
    Host maker;
    maker.edit ("ratio", infinity);
    maker.edit ("threshold", -31.5f);
    maker.edit ("attack", 0.5f);
    maker.edit ("lookahead", 3.25f);
    const auto file = maker.session.presetLibrary().save ("Squash", maker.session.presetState());
    REQUIRE (file.has_value());
    CHECK (file->getFileExtension() == ".comp1preset");
    CHECK (file->isAChildOf (maker.temporary.folder));

    Host host;
    const auto preset = host.session.presetLibrary().read (*file);
    REQUIRE (preset.isValid());
    REQUIRE (host.session.loadPreset (preset, "Squash"));
    for (const auto& id : presetSettingIds (*host.processor))
    {
        CAPTURE (id);
        CHECK (host.value (id) == maker.value (id));
    }
    CHECK (host.value ("ratio") == infinity);
    CHECK (host.session.loadedPresetName() == "Squash");
    CHECK_FALSE (host.session.isLoadedPresetModified());
    CHECK (host.history.undoSteps() == 1);
    host.history.undo();
    CHECK (host.value ("ratio") == 4.0f);
    CHECK (host.value ("threshold") == host::parameter (*host.processor, "threshold").convertFrom0to1 (host::parameter (*host.processor, "threshold").getDefaultValue()));
}

TEST_CASE ("A tree that isn't comp1's changes nothing when loaded as a Preset")
{
    Host host;
    host.edit ("threshold", -12.0f);
    auto other = host.session.presetState().createCopy();
    juce::ValueTree foreign ("eq1");
    foreign.copyPropertiesAndChildrenFrom (other, nullptr);
    foreign.getChildWithProperty ("id", "threshold").setProperty ("value", -40.0f, nullptr);
    CHECK_FALSE (host.session.loadPreset (foreign, "Foreign"));
    CHECK_THAT (host.value ("threshold"), WithinAbs (-12.0f, 1e-4));
    CHECK (host.session.loadedPresetName().isEmpty());
    CHECK (host.history.undoSteps() == 1);
}

TEST_CASE ("A/B Compare holds two independent sets, each with its own Loaded Preset and Modified state")
{
    Host maker;
    maker.edit ("threshold", -20.0f);
    const auto preset = maker.session.presetState();

    Host host;
    host.session.selectCompareSide (CompareSide::B);
    CHECK (host.history.undoSteps() == 1);
    host.edit ("threshold", -6.0f);
    host.session.selectCompareSide (CompareSide::A);
    CHECK (host.history.undoSteps() == 3);
    CHECK (host.value ("threshold") != -6.0f);

    REQUIRE (host.session.loadPreset (preset, "Gentle"));
    host.edit ("knee", 12.0f);
    CHECK (host.session.isLoadedPresetModified());

    host.session.selectCompareSide (CompareSide::B);
    CHECK_THAT (host.value ("threshold"), WithinAbs (-6.0f, 1e-4));
    CHECK (host.value ("knee") != 12.0f);
    CHECK (host.session.loadedPresetName().isEmpty());
    CHECK_FALSE (host.session.isLoadedPresetModified());

    host.session.selectCompareSide (CompareSide::A);
    CHECK_THAT (host.value ("threshold"), WithinAbs (-20.0f, 1e-4));
    CHECK_THAT (host.value ("knee"), WithinAbs (12.0f, 1e-4));
    CHECK (host.session.loadedPresetName() == "Gentle");
    CHECK (host.session.isLoadedPresetModified());

    // A Copy is one undo step, and puts A's settings and Loaded Preset on B.
    const int before = host.history.undoSteps();
    host.session.copyToOther();
    CHECK (host.history.undoSteps() == before + 1);
    host.session.selectCompareSide (CompareSide::B);
    CHECK_THAT (host.value ("threshold"), WithinAbs (-20.0f, 1e-4));
    CHECK (host.session.loadedPresetName() == "Gentle");
    host.history.undo();
    CHECK (host.session.compareSide() == CompareSide::A);
    host.history.undo();
    host.session.selectCompareSide (CompareSide::B);
    CHECK_THAT (host.value ("threshold"), WithinAbs (-6.0f, 1e-4));
}

TEST_CASE ("The A/B side, both sides' settings and both Loaded Presets survive save and reload")
{
    Host maker;
    maker.edit ("threshold", -20.0f);
    const auto gentle = maker.session.presetState();
    maker.edit ("threshold", -35.0f);
    const auto hard = maker.session.presetState();

    Host saved;
    REQUIRE (saved.session.loadPreset (gentle, "Gentle"));
    saved.session.selectCompareSide (CompareSide::B);
    REQUIRE (saved.session.loadPreset (hard, "Hard"));
    saved.edit ("ratio", infinity);
    const auto state = host::saved (*saved.processor);

    Host restored;
    host::restore (*restored.processor, state);
    CHECK (restored.session.compareSide() == CompareSide::B);
    CHECK_THAT (restored.value ("threshold"), WithinAbs (-35.0f, 1e-4));
    CHECK (restored.value ("ratio") == infinity);
    CHECK (restored.session.loadedPresetName() == "Hard");
    CHECK (restored.session.isLoadedPresetModified());
    restored.session.selectCompareSide (CompareSide::A);
    CHECK_THAT (restored.value ("threshold"), WithinAbs (-20.0f, 1e-4));
    CHECK (restored.session.loadedPresetName() == "Gentle");
    CHECK_FALSE (restored.session.isLoadedPresetModified());
}

TEST_CASE ("The reported latency follows the A/B side and a loaded Preset, and undo")
{
    Host host;
    host.processor->prepareToPlay (96000.0, 512);
    const int highQualityLatency = comp1::latencySamples (comp1::ProcessingMode::HighQuality, 96000.0);
    REQUIRE (highQualityLatency > 0);

    SECTION ("Switching to a side in High Quality")
    {
        host.session.selectCompareSide (CompareSide::B);
        host.edit ("processing_mode", highQuality);
        host.session.selectCompareSide (CompareSide::A);
        CHECK (host.processor->getLatencySamples() == 0);
        host.session.selectCompareSide (CompareSide::B);
        CHECK (host.processor->getLatencySamples() == highQualityLatency);
        host.history.undo();
        CHECK (host.processor->getLatencySamples() == 0);
    }

    SECTION ("Loading a Preset saved in High Quality")
    {
        Host maker;
        maker.edit ("processing_mode", highQuality);
        REQUIRE (host.session.loadPreset (maker.session.presetState(), "Mastering"));
        CHECK (host.processor->getLatencySamples() == highQualityLatency);
        host.history.undo();
        CHECK (host.value ("processing_mode") == zeroLatency);
        CHECK (host.processor->getLatencySamples() == 0);
    }
}

TEST_CASE ("An edit in a gesture is one undo step; Host Automation is none, and restoring a session empties the history")
{
    Host host;
    host.edit ("attack", 30.0f);
    CHECK (host.history.undoSteps() == 1);
    host.history.undo();
    CHECK (host.value ("attack") != 30.0f);
    REQUIRE (host.history.canRedo());
    host.history.redo();
    CHECK_THAT (host.value ("attack"), WithinAbs (30.0f, 1e-4));

    host::set (*host.processor, "release", 400.0f);
    CHECK (host.history.undoSteps() == 1);

    host::restore (*host.processor, host::saved (*host.processor));
    CHECK_FALSE (host.history.canUndo());
    CHECK_FALSE (host.history.canRedo());
}

TEST_CASE ("Restoring a session lets go of Detection Audition; a state that isn't comp1's changes nothing")
{
    Host host;
    const auto state = host::saved (*host.processor);
    host.processor->setDetectionAudition (true);
    host.edit ("threshold", -10.0f);

    juce::XmlElement foreign ("eq1");
    foreign.setAttribute ("version", 2);
    host::restore (*host.processor, foreign);
    CHECK (host.processor->detectionAudition());
    CHECK_THAT (host.value ("threshold"), WithinAbs (-10.0f, 1e-4));
    CHECK (host.history.canUndo());

    host::restore (*host.processor, state);
    CHECK_FALSE (host.processor->detectionAudition());
    CHECK (host.value ("threshold") != -10.0f);
}
