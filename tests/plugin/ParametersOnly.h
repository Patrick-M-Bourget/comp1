#pragma once

#include "Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace host
{

// A processor with nothing but comp1's host parameters, so a test can read them through SettingsReader as the Plugin
// Shell does, without its Engine.
class ParametersOnly final : public juce::AudioProcessor
{
public:
    const juce::String getName() const override { return "parameters only"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState parameters { *this, nullptr, "comp1", comp1::parameters::createLayout() };
};

} // namespace host
