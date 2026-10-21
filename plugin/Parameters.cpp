#include "Parameters.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace comp1::parameters
{

namespace
{

// A range that moves evenly.
juce::NormalisableRange<float> linearRange (ParameterRange range)
{
    return { static_cast<float> (range.low), static_cast<float> (range.high) };
}

// A range that moves evenly in log space, as times, frequencies and Q are heard: each doubling of Attack, Release,
// a Frequency or Q takes the same travel. The maths runs in double, so a value set by the host survives save and reload exactly.
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

// A range in steps of step from its low end, as the Detection EQ's Slopes move.
juce::NormalisableRange<float> steppedRange (ParameterRange range, float step)
{
    return { static_cast<float> (range.low), static_cast<float> (range.high), step };
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

// A level in dB down to Off, Dry's and Wet's: the travel is even in dB from levelTravelFloorDb at its bottom to the
// range's top, and its bottom is Off (-inf), reached exactly, so a host's 0 is Off. At the floor, a path summed with
// the other at unity moves the level by under 0.03 dB. The floor makes the span 64 dB, a power of two, so 0 dB, the
// default Wet, and every quarter dB sit at travel a float holds exactly, and pass through it unrounded. The maths
// runs in double, as logRange's does.
constexpr double levelTravelFloorDb = -52.0;
juce::NormalisableRange<float> levelRange (ParameterRange range)
{
    const double span = range.high - levelTravelFloorDb;
    return { static_cast<float> (range.low),
             static_cast<float> (range.high),
             [span] (float, float, float normalised) {
                 return normalised <= 0.0f ? -std::numeric_limits<float>::infinity()
                                           : static_cast<float> (levelTravelFloorDb + span * double { normalised });
             },
             [span] (float, float, float value) {
                 if (value <= levelTravelFloorDb)
                     return 0.0f;
                 return static_cast<float> (std::min (1.0, (double { value } - levelTravelFloorDb) / span));
             },
             [] (float, float, float value) { return value <= levelTravelFloorDb ? -std::numeric_limits<float>::infinity() : value; } };
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

// A level's text: Off at -inf, which "Off" and "-inf" read as too; dB elsewhere.
juce::AudioParameterFloatAttributes levelText()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel ("dB")
        .withStringFromValueFunction ([] (float value, int) { return std::isinf (value) ? juce::String ("Off") : juce::String (value, 2); })
        .withValueFromStringFunction ([] (const juce::String& text) {
            const auto trimmed = text.trim();
            if (trimmed.equalsIgnoreCase ("off") || trimmed.equalsIgnoreCase ("-inf"))
                return -std::numeric_limits<float>::infinity();
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

// The choice index of the free band's Shape that designs as shape.
int freeBandShapeIndex (plugkit::Shape shape)
{
    for (const auto choice : { FreeBandShape::Bell, FreeBandShape::LowShelf, FreeBandShape::HighShelf })
        if (shapeOf (choice) == shape)
            return static_cast<int> (choice);
    return static_cast<int> (FreeBandShape::Bell);
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
                                                              static_cast<int> (defaults.detectionSource)));
    // The Detection EQ: the high-pass, the low-pass and the free band, each with its own on/off. The free band's
    // Shapes are FreeBandShape's, in its order, which sessions save by index.
    const auto& eq = defaults.detectionEq;
    const auto onOff = [] (const juce::String& id, const juce::String& name, bool defaultValue) {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
    };
    const auto frequency = [] (const juce::String& id, const juce::String& name, double defaultValue) {
        return floatParameter (id, name, logRange (parameterRanges::detectionFrequency), defaultValue, withText (1, "Hz"));
    };
    const auto slope = [] (const juce::String& id, const juce::String& name, double defaultValue) {
        return floatParameter (id, name, steppedRange (parameterRanges::detectionSlope, 6.0f), defaultValue, withText (0, "dB/oct"));
    };
    layout.add (onOff (detectionHighPassOnId, "Detection High-Pass", eq.highPass.on),
                frequency (detectionHighPassFrequencyId, "Detection High-Pass Frequency", eq.highPass.filter.frequency),
                slope (detectionHighPassSlopeId, "Detection High-Pass Slope", eq.highPass.filter.slope),
                onOff (detectionLowPassOnId, "Detection Low-Pass", eq.lowPass.on),
                frequency (detectionLowPassFrequencyId, "Detection Low-Pass Frequency", eq.lowPass.filter.frequency),
                slope (detectionLowPassSlopeId, "Detection Low-Pass Slope", eq.lowPass.filter.slope),
                onOff (detectionBandOnId, "Detection Free Band", eq.freeBand.on),
                std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { detectionBandShapeId, 1 },
                                                              "Detection Free Band Shape",
                                                              juce::StringArray { "Bell", "Low Shelf", "High Shelf" },
                                                              freeBandShapeIndex (eq.freeBand.filter.shape)),
                frequency (detectionBandFrequencyId, "Detection Free Band Frequency", eq.freeBand.filter.frequency),
                floatParameter (detectionBandGainId, "Detection Free Band Gain", linearRange (parameterRanges::detectionGain), eq.freeBand.filter.gain, withText (2, "dB")),
                floatParameter (detectionBandQId, "Detection Free Band Q", logRange (parameterRanges::detectionQ), eq.freeBand.filter.q, withText (3, {})));
    // Its choices are StereoPlacement's, in its order, which sessions save by index: comp1 offers Stereo, Mid and Side.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stereoPlacementId, 1 },
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
    // Dry and Wet, the levels the two paths are summed at.
    layout.add (floatParameter (dryId, "Dry", levelRange (parameterRanges::dry), defaults.dryDb, levelText()),
                floatParameter (wetId, "Wet", levelRange (parameterRanges::wet), defaults.wetDb, levelText()));
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
    // Not JUCE's bypass parameter (the processor doesn't override getBypassParameter()), so the host's own bypass
    // stays separate from it.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { globalBypassId, 1 }, "Global Bypass", defaults.globalBypass));
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
      dry (parameters.getRawParameterValue (dryId)),
      wet (parameters.getRawParameterValue (wetId)),
      outputGain (parameters.getRawParameterValue (outputGainId)),
      detectionSource (parameters.getRawParameterValue (detectionSourceId)),
      highPassOn (parameters.getRawParameterValue (detectionHighPassOnId)),
      highPassFrequency (parameters.getRawParameterValue (detectionHighPassFrequencyId)),
      highPassSlope (parameters.getRawParameterValue (detectionHighPassSlopeId)),
      lowPassOn (parameters.getRawParameterValue (detectionLowPassOnId)),
      lowPassFrequency (parameters.getRawParameterValue (detectionLowPassFrequencyId)),
      lowPassSlope (parameters.getRawParameterValue (detectionLowPassSlopeId)),
      bandOn (parameters.getRawParameterValue (detectionBandOnId)),
      bandShape (parameters.getRawParameterValue (detectionBandShapeId)),
      bandFrequency (parameters.getRawParameterValue (detectionBandFrequencyId)),
      bandGain (parameters.getRawParameterValue (detectionBandGainId)),
      bandQ (parameters.getRawParameterValue (detectionBandQId)),
      stereoPlacement (parameters.getRawParameterValue (stereoPlacementId)),
      stereoLink (parameters.getRawParameterValue (stereoLinkId)),
      processingMode (parameters.getRawParameterValue (processingModeId)),
      lookahead (parameters.getRawParameterValue (lookaheadId)),
      globalBypass (parameters.getRawParameterValue (globalBypassId))
{
}

Settings SettingsReader::read() const
{
    const auto plain = [] (const std::atomic<float>* value) { return static_cast<double> (value->load (std::memory_order_relaxed)); };
    const auto on = [] (const std::atomic<float>* value) { return value->load (std::memory_order_relaxed) >= 0.5f; };
    // A choice's index, as its enum, the choices in the enum's order up to last.
    const auto choice = [&plain]<typename Choice> (const std::atomic<float>* value, Choice last) {
        return static_cast<Choice> (std::clamp (static_cast<int> (std::lround (plain (value))), 0, static_cast<int> (last)));
    };
    // Each band's filter starts from the defaults, which hold what no host parameter sets (the cuts' Q, the shelves'
    // Slope).
    DetectionEq eq;
    eq.highPass.on = on (highPassOn);
    eq.highPass.filter.frequency = plain (highPassFrequency);
    eq.highPass.filter.slope = plain (highPassSlope);
    eq.lowPass.on = on (lowPassOn);
    eq.lowPass.filter.frequency = plain (lowPassFrequency);
    eq.lowPass.filter.slope = plain (lowPassSlope);
    eq.freeBand.on = on (bandOn);
    eq.freeBand.filter.shape = shapeOf (choice (bandShape, FreeBandShape::HighShelf));
    eq.freeBand.filter.frequency = plain (bandFrequency);
    eq.freeBand.filter.gain = plain (bandGain);
    eq.freeBand.filter.q = plain (bandQ);
    return { .thresholdDb = plain (threshold),
             .ratio = plain (ratio),
             .kneeDb = plain (knee),
             .attackMs = plain (attack),
             .releaseMs = plain (release),
             .autoRelease = on (autoRelease),
             .holdMs = plain (hold),
             .rangeDb = plain (range),
             .autoGain = on (autoGain),
             .saturation = on (saturation),
             .driveDb = plain (drive),
             .saturationPosition = choice (saturationPosition, SaturationPosition::After),
             .dryDb = plain (dry),
             .wetDb = plain (wet),
             .outputGainDb = plain (outputGain),
             .detectionSource = choice (detectionSource, DetectionSource::External),
             .detectionEq = eq,
             .stereoPlacement = choice (stereoPlacement, StereoPlacement::Side),
             .stereoLink = plain (stereoLink),
             .processingMode = choice (processingMode, ProcessingMode::HighQuality),
             .lookaheadMs = plain (lookahead),
             .globalBypass = on (globalBypass) };
}

} // namespace comp1::parameters
