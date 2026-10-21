#include "Parameters.h"

#include <algorithm>
#include <cmath>

namespace comp1::parameters
{

namespace
{

// A range that moves evenly.
juce::NormalisableRange<float> linearRange (ParameterRange range)
{
    return { static_cast<float> (range.low), static_cast<float> (range.high) };
}

// A range that moves evenly in log space, as times are heard: each doubling of Attack or Release takes the same
// travel. The maths runs in double, so a value set by the host survives save and reload exactly.
juce::NormalisableRange<float> logRange (ParameterRange range)
{
    return { static_cast<float> (range.low),
             static_cast<float> (range.high),
             [] (float start, float end, float normalised) {
                 return static_cast<float> (start * std::pow (double { end } / start, double { normalised }));
             },
             [] (float start, float end, float value) {
                 return static_cast<float> (std::log (double { value } / start) / std::log (double { end } / start));
             },
             [] (float, float, float value) { return value; } };
}

// A range from 0 that gives its low end more travel: halfway is centre.
juce::NormalisableRange<float> skewedRange (ParameterRange range, float centre)
{
    auto skewed = linearRange (range);
    skewed.setSkewForCentre (centre);
    return skewed;
}

// Ratio, 1:1 to ∞:1: the travel is the square of 1 - 1/Ratio, the share of the level over Threshold taken off. So ∞:1
// sits at the top, reached exactly, and the low Ratios, where a step is heard most, get the most travel (2:1 at a
// quarter, 4:1 just past half). The maths runs in double, as logRange's does.
juce::NormalisableRange<float> ratioRange()
{
    return { static_cast<float> (parameterRanges::ratio.low),
             static_cast<float> (parameterRanges::ratio.high),
             [] (float, float, float normalised) {
                 return static_cast<float> (1.0 / (1.0 - std::sqrt (double { normalised })));
             },
             [] (float, float, float value) {
                 const double taken = 1.0 - 1.0 / double { value };
                 return static_cast<float> (taken * taken);
             },
             [] (float, float, float value) { return value; } };
}

// Fixed-precision text, so value -> text -> value -> text round-trips.
juce::AudioParameterFloatAttributes withText (int decimals, const juce::String& label)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (label)
        .withStringFromValueFunction ([decimals] (float value, int) { return juce::String (value, decimals); })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

// Ratio's text: ∞ at the top, which inf reads as too.
juce::AudioParameterFloatAttributes ratioText()
{
    static const juce::String infinitySign { juce::CharPointer_UTF8 ("\xe2\x88\x9e") };
    return juce::AudioParameterFloatAttributes()
        .withLabel (":1")
        .withStringFromValueFunction ([] (float value, int) { return std::isinf (value) ? infinitySign : juce::String (value, 2); })
        .withValueFromStringFunction ([] (const juce::String& text) {
            const auto trimmed = text.trim();
            if (trimmed == infinitySign || trimmed.equalsIgnoreCase ("inf"))
                return std::numeric_limits<float>::infinity();
            return trimmed.getFloatValue();
        });
}

std::unique_ptr<juce::AudioParameterFloat> floatParameter (const juce::String& id,
                                                           const juce::String& name,
                                                           juce::NormalisableRange<float> range,
                                                           double defaultValue,
                                                           juce::AudioParameterFloatAttributes attributes)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, static_cast<float> (defaultValue), attributes);
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    // Each default is Settings' own, so the host's defaults and the Engine's are one.
    const Settings defaults;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Its choices are DetectionSource's, in its order, which sessions save by index.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { detectionSourceId, 1 },
                                                              "Detection Source",
                                                              juce::StringArray { "Internal", "External" },
                                                              static_cast<int> (defaults.detectionSource)),
                // Its choices are StereoPlacement's, in its order, which sessions save by index: comp1 offers Stereo, Mid
                // and Side.
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stereoPlacementId, 1 },
                                                              "Stereo Placement",
                                                              juce::StringArray { "Stereo", "Mid", "Side" },
                                                              static_cast<int> (defaults.stereoPlacement)),
                floatParameter (stereoLinkId, "Stereo Link", linearRange (parameterRanges::stereoLink), defaults.stereoLink, withText (1, "%")));
    layout.add (floatParameter (thresholdId, "Threshold", linearRange (parameterRanges::threshold), defaults.thresholdDb, withText (2, "dB")),
                floatParameter (ratioId, "Ratio", ratioRange(), defaults.ratio, ratioText()),
                floatParameter (kneeId, "Knee", linearRange (parameterRanges::knee), defaults.kneeDb, withText (2, "dB")),
                floatParameter (attackId, "Attack", logRange (parameterRanges::attack), defaults.attackMs, withText (3, "ms")),
                floatParameter (releaseId, "Release", logRange (parameterRanges::release), defaults.releaseMs, withText (1, "ms")),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoReleaseId, 1 }, "Auto Release", defaults.autoRelease),
                // Hold starts at 0 ms, which a log range can't reach; 50 ms sits at its centre.
                floatParameter (holdId, "Hold", skewedRange (parameterRanges::hold, 50.0f), defaults.holdMs, withText (1, "ms")),
                floatParameter (rangeId, "Range", linearRange (parameterRanges::range), defaults.rangeDb, withText (2, "dB")),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoGainId, 1 }, "Auto Gain", defaults.autoGain));
    // Saturation Position's choices are SaturationPosition's, stored in sessions by index, so their order never
    // changes (ADR 0003).
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { saturationId, 1 }, "Saturation", defaults.saturation),
                floatParameter (driveId, "Drive", linearRange (parameterRanges::drive), defaults.driveDb, withText (2, "dB")),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { saturationPositionId, 1 },
                                                              "Saturation Position",
                                                              juce::StringArray { "Before", "After" },
                                                              static_cast<int> (defaults.saturationPosition)));
    // Linear in dB, symmetric about 0 dB, which is the default and the range's exact centre: the default passes audio
    // through bit for bit.
    layout.add (floatParameter (outputGainId, "Output Gain", linearRange (parameterRanges::outputGain), defaults.outputGainDb, withText (2, "dB")));
    // Processing Mode's choices are stored in sessions by index, so their order never changes (ADR 0003). Hosts can't
    // automate it: a switch changes the latency the host compensates for, and fades the output out and in.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { processingModeId, 1 },
                                                              "Processing Mode",
                                                              juce::StringArray { "Zero Latency", "High Quality" },
                                                              static_cast<int> (defaults.processingMode),
                                                              juce::AudioParameterChoiceAttributes().withAutomatable (false)),
                floatParameter (lookaheadId, "Lookahead", linearRange (parameterRanges::lookahead), defaults.lookaheadMs, withText (2, "ms")));
    return layout;
}

SettingsReader::SettingsReader (juce::AudioProcessorValueTreeState& parameters)
    : threshold (parameters.getRawParameterValue (thresholdId)),
      ratio (parameters.getRawParameterValue (ratioId)),
      knee (parameters.getRawParameterValue (kneeId)),
      attack (parameters.getRawParameterValue (attackId)),
      release (parameters.getRawParameterValue (releaseId)),
      autoRelease (parameters.getRawParameterValue (autoReleaseId)),
      hold (parameters.getRawParameterValue (holdId)),
      range (parameters.getRawParameterValue (rangeId)),
      autoGain (parameters.getRawParameterValue (autoGainId)),
      saturation (parameters.getRawParameterValue (saturationId)),
      drive (parameters.getRawParameterValue (driveId)),
      saturationPosition (parameters.getRawParameterValue (saturationPositionId)),
      outputGain (parameters.getRawParameterValue (outputGainId)),
      detectionSource (parameters.getRawParameterValue (detectionSourceId)),
      stereoPlacement (parameters.getRawParameterValue (stereoPlacementId)),
      stereoLink (parameters.getRawParameterValue (stereoLinkId)),
      processingMode (parameters.getRawParameterValue (processingModeId)),
      lookahead (parameters.getRawParameterValue (lookaheadId))
{
}

Settings SettingsReader::read() const
{
    const auto plain = [] (const std::atomic<float>* value) { return static_cast<double> (value->load (std::memory_order_relaxed)); };
    return { .thresholdDb = plain (threshold),
             .ratio = plain (ratio),
             .kneeDb = plain (knee),
             .attackMs = plain (attack),
             .releaseMs = plain (release),
             .autoRelease = autoRelease->load (std::memory_order_relaxed) >= 0.5f,
             .holdMs = plain (hold),
             .rangeDb = plain (range),
             .autoGain = autoGain->load (std::memory_order_relaxed) >= 0.5f,
             .saturation = saturation->load (std::memory_order_relaxed) >= 0.5f,
             .driveDb = plain (drive),
             .saturationPosition = saturationPosition->load (std::memory_order_relaxed) < 0.5f ? SaturationPosition::Before
                                                                                               : SaturationPosition::After,
             .outputGainDb = plain (outputGain),
             .detectionSource = detectionSource->load (std::memory_order_relaxed) >= 0.5f ? DetectionSource::External : DetectionSource::Internal,
             .stereoPlacement = static_cast<StereoPlacement> (std::clamp (static_cast<int> (std::lround (stereoPlacement->load (std::memory_order_relaxed))), 0, 2)),
             .stereoLink = plain (stereoLink),
             .processingMode = processingMode->load (std::memory_order_relaxed) >= 0.5f ? ProcessingMode::HighQuality : ProcessingMode::ZeroLatency,
             .lookaheadMs = plain (lookahead) };
}

} // namespace comp1::parameters
