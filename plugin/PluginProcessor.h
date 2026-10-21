#pragma once

#include "Parameters.h"
#include "comp1/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace comp1
{

// The Plugin Shell: the JUCE processor around the Engine. It turns the host parameters into Settings once a
// block, and saves and restores them.
class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

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

    // The version of the saved state's format. setStateInformation() brings older states up to it one version at
    // a time, and loads what it knows of newer ones. Bump it, and add a step to the migration, whenever the format
    // changes.
    static constexpr int stateVersion = 1;

    void getStateInformation (juce::MemoryBlock& destData) override;
    // Restoring a state lets go of Detection Audition.
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Detection Audition, while the editor holds it: not a host parameter, and never saved. Any thread.
    void setDetectionAudition (bool held) { heldDetectionAudition.store (held, std::memory_order_relaxed); }
    bool detectionAudition() const { return heldDetectionAudition.load (std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState parameters;
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
