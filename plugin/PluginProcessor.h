#pragma once

#include "Parameters.h"
#include "comp1/Engine.h"
#include "plugkit/ui/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace comp1
{

// The Plugin Shell: the JUCE processor around the Engine. It turns the host parameters into Settings once a
// block, and saves, restores and edits them through its Session: User Presets, A/B Compare and undo and redo.
class PluginProcessor final : public juce::AudioProcessor
{
public:
    // As it ships: the per-user settings in UserSettings::defaultFile ("comp1"), and User Presets in
    // Documents/comp1/Presets.
    PluginProcessor();
    // With the per-user settings file and the User Preset folder given, so a test keeps both in a folder of its own.
    PluginProcessor (juce::File userSettingsFile, juce::File presetFolder);
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
    // The host's own bypass: played as processBlock() plays, with Global Bypass forced on, so it is delayed by the
    // reported latency in each Processing Mode and crossfades in and out click-free. The Global Bypass host parameter
    // isn't written.
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlockBypassed;

    // A placeholder: the host parameters, as JUCE's generic editor lays them out.
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // The version of the saved state's format, and of a Preset's. Restoring a session or loading a Preset brings an
    // older one up to it one version at a time, and loads what it knows of a newer one. Bump it, and add a step to
    // the migration, whenever the format changes. Version 1 is the parameters' state with its version; version 2
    // adds A/B Compare.
    static constexpr int stateVersion = 2;

    // The editor window's logical size, and its limits: a placeholder until the Expanded View sets its own.
    static constexpr plugkit::ui::Session::Window window { { 1200, 760 }, { 960, 600 }, { 2560, 1600 } };

    void getStateInformation (juce::MemoryBlock& destData) override;
    // Restoring a state of comp1's lets go of Detection Audition, then restores through the Session and reports the
    // restored Processing Mode's latency. Anything else changes nothing.
    void setStateInformation (const void* data, int sizeInBytes) override;

    // The session state the editor edits through: the undo history, A/B Compare and the Preset Library. A Preset
    // load, an A/B switch or Copy, and their undo and redo may change Processing Mode, and the reported latency
    // follows it.
    plugkit::ui::Session& session() { return sessionState; }

    // Whether the editor offers Stereo Placement and Stereo Link: only on a stereo track. On a mono one both keep
    // their stored values and change nothing, the mono channel compressed as Stereo.
    bool isStereoPlacementAvailable() const { return getMainBusNumOutputChannels() == 2; }
    bool isStereoLinkAvailable() const { return getMainBusNumOutputChannels() == 2; }

    // Detection Audition, while the editor holds it: not a host parameter, and never saved. Any thread.
    void setDetectionAudition (bool held) { heldDetectionAudition.store (held, std::memory_order_relaxed); }
    bool detectionAudition() const { return heldDetectionAudition.load (std::memory_order_relaxed); }

private:
    // processBlock(), with Global Bypass forced on while the host bypasses.
    void play (juce::AudioBuffer<float>& buffer, bool hostBypassed);

    juce::AudioProcessorValueTreeState parameters;
    plugkit::ui::Session sessionState; // built after the parameters it holds
    parameters::SettingsReader reader { parameters }; // read on the audio thread
    Engine engine;
    // The last Settings handed to the Engine, kept on the audio thread; the first block after prepareToPlay()
    // hands over whatever it reads.
    Settings publishedSettings;
    bool publishedSincePrepare = false;
    std::atomic<bool> heldDetectionAudition { false };

    // Reports to the host the latency of the Processing Mode the host parameter holds (comp1/Latency.h), at the
    // sample rate last prepared: at prepareToPlay(), whenever Processing Mode changes (from the editor, or a host that
    // sets it anyway), and after a state restore. Any thread may call it: setLatencySamples() is safe from any, as
    // JUCE's wrappers pass the change to the host on the message thread.
    void reportLatency();
    std::atomic<double> preparedSampleRate { 0.0 };
    struct ProcessingModeListener final : juce::AudioProcessorValueTreeState::Listener
    {
        explicit ProcessingModeListener (PluginProcessor& p) : processor (p) {}
        void parameterChanged (const juce::String&, float) override { processor.reportLatency(); }
        PluginProcessor& processor;
    } processingModeListener { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace comp1
