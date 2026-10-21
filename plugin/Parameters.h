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
inline const juce::String autoReleaseId { "auto_release" };
inline const juce::String holdId { "hold" };
inline const juce::String rangeId { "range" };
inline const juce::String autoGainId { "auto_gain" };
inline const juce::String saturationId { "saturation" };
inline const juce::String driveId { "drive" };
inline const juce::String saturationPositionId { "saturation_position" };
inline const juce::String outputGainId { "output_gain" };
inline const juce::String detectionSourceId { "detection_source" };
inline const juce::String detectionHighPassOnId { "detection_high_pass_on" };
inline const juce::String detectionHighPassFrequencyId { "detection_high_pass_frequency" };
inline const juce::String detectionHighPassSlopeId { "detection_high_pass_slope" };
inline const juce::String detectionLowPassOnId { "detection_low_pass_on" };
inline const juce::String detectionLowPassFrequencyId { "detection_low_pass_frequency" };
inline const juce::String detectionLowPassSlopeId { "detection_low_pass_slope" };
inline const juce::String detectionBandOnId { "detection_band_on" };
inline const juce::String detectionBandShapeId { "detection_band_shape" };
inline const juce::String detectionBandFrequencyId { "detection_band_frequency" };
inline const juce::String detectionBandGainId { "detection_band_gain" };
inline const juce::String detectionBandQId { "detection_band_q" };
inline const juce::String stereoPlacementId { "stereo_placement" };
inline const juce::String stereoLinkId { "stereo_link" };
inline const juce::String processingModeId { "processing_mode" };
inline const juce::String lookaheadId { "lookahead" };
inline const juce::String oversamplingId { "oversampling" };

// Every host parameter, in the order hosts list them: Detection Source, the Detection EQ, Stereo Placement and Stereo
// Link, then the compressor's, then Saturation's, then Output Gain, as the signal meets them; then the whole plugin's,
// Processing Mode, Lookahead and Oversampling.
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
    std::atomic<float>* autoRelease;
    std::atomic<float>* hold;
    std::atomic<float>* range;
    std::atomic<float>* autoGain;
    std::atomic<float>* saturation;
    std::atomic<float>* drive;
    std::atomic<float>* saturationPosition;
    std::atomic<float>* outputGain;
    std::atomic<float>* detectionSource;
    std::atomic<float>* highPassOn;
    std::atomic<float>* highPassFrequency;
    std::atomic<float>* highPassSlope;
    std::atomic<float>* lowPassOn;
    std::atomic<float>* lowPassFrequency;
    std::atomic<float>* lowPassSlope;
    std::atomic<float>* bandOn;
    std::atomic<float>* bandShape;
    std::atomic<float>* bandFrequency;
    std::atomic<float>* bandGain;
    std::atomic<float>* bandQ;
    std::atomic<float>* stereoPlacement;
    std::atomic<float>* stereoLink;
    std::atomic<float>* processingMode;
    std::atomic<float>* lookahead;
    std::atomic<float>* oversampling;
};

} // namespace comp1::parameters
