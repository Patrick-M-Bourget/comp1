#pragma once

#include "comp1/Settings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace comp1::parameters
{

// Parameter IDs are stable: hosts store them in sessions and automation, so they never change.
inline const juce::String outputGainId { "output_gain" };

// Every host parameter, in the order hosts list them.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// The host parameters as Settings, each resolved once. Reads from any thread, with no allocation or lock.
class SettingsReader
{
public:
    explicit SettingsReader (juce::AudioProcessorValueTreeState& parameters);

    Settings read() const;

private:
    std::atomic<float>* outputGain;
};

} // namespace comp1::parameters
