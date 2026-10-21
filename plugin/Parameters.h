#pragma once

#include "comp1/Settings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace comp1::parameters
{

// Parameter IDs are stable: hosts store them in sessions and automation, so they never change.
inline const juce::String thresholdId { "threshold" };
inline const juce::String ratioId { "ratio" };
inline const juce::String kneeId { "knee" };
inline const juce::String attackId { "attack" };
inline const juce::String releaseId { "release" };
inline const juce::String holdId { "hold" };
inline const juce::String rangeId { "range" };
inline const juce::String autoGainId { "auto_gain" };
inline const juce::String outputGainId { "output_gain" };
inline const juce::String detectionSourceId { "detection_source" };

// Every host parameter, in the order hosts list them: Detection Source, then the compressor's, then Output Gain, as the
// signal meets them.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// The host parameters as Settings, each resolved once. Reads from any thread, with no allocation or lock.
class SettingsReader
{
public:
    explicit SettingsReader (juce::AudioProcessorValueTreeState& parameters);

    Settings read() const;

private:
    std::atomic<float>* threshold;
    std::atomic<float>* ratio;
    std::atomic<float>* knee;
    std::atomic<float>* attack;
    std::atomic<float>* release;
    std::atomic<float>* hold;
    std::atomic<float>* range;
    std::atomic<float>* autoGain;
    std::atomic<float>* outputGain;
    std::atomic<float>* detectionSource;
};

} // namespace comp1::parameters
