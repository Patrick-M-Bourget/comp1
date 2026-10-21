#include "Parameters.h"

namespace comp1::parameters
{

namespace
{

// A range that moves evenly.
juce::NormalisableRange<float> linearRange (ParameterRange range)
{
    return { static_cast<float> (range.low), static_cast<float> (range.high) };
}

// Fixed-precision text, so value -> text -> value -> text round-trips.
juce::AudioParameterFloatAttributes withText (int decimals, const juce::String& label)
{
    return juce::AudioParameterFloatAttributes()
        .withLabel (label)
        .withStringFromValueFunction ([decimals] (float value, int) { return juce::String (value, decimals); })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Linear in dB, symmetric about 0 dB, which is the default and the range's exact centre: the default passes
    // audio through bit for bit.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { outputGainId, 1 },
                                                             "Output Gain",
                                                             linearRange (parameterRanges::outputGain),
                                                             0.0f,
                                                             withText (2, "dB")));
    return layout;
}

SettingsReader::SettingsReader (juce::AudioProcessorValueTreeState& parameters)
    : outputGain (parameters.getRawParameterValue (outputGainId))
{
}

Settings SettingsReader::read() const
{
    return { .outputGainDb = static_cast<double> (outputGain->load (std::memory_order_relaxed)) };
}

} // namespace comp1::parameters
