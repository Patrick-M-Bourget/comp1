#pragma once

// The plugin as a host has it, with its Session: what the Session's tests and the Style's share.

#include "Host.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace host
{

// A folder of its own for each test's per-user settings and User Presets, deleted after it, so no test reaches the
// real Documents folder or settings file. Named by a UUID, as tests run in parallel processes.
struct TemporaryFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("comp1-session-test-" + juce::Uuid().toString());
    TemporaryFolder() { REQUIRE (folder.createDirectory()); }
    ~TemporaryFolder() { folder.deleteRecursively(); }
};

// The plugin as a host has it, its session kept in a temporary folder.
struct SessionHost
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
inline juce::StringArray presetSettingIds (juce::AudioProcessor& processor)
{
    juce::StringArray ids;
    for (auto* p : processor.getParameters())
        if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (p);
            ranged != nullptr && ranged->getParameterID() != plugkit::ui::globalBypassId)
            ids.add (ranged->getParameterID());
    return ids;
}

} // namespace host
