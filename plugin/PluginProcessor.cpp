#include "PluginProcessor.h"

#include <cmath>

namespace comp1
{

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "comp1", parameters::createLayout())
{
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock, getMainBusNumOutputChannels());
    publishedSincePrepare = false;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono and stereo tracks, the same in and out, with a stereo, mono or no Sidechain.
    const auto& main = layouts.getMainOutputChannelSet();
    const auto sidechain = layouts.getChannelSet (true, 1);
    return layouts.getMainInputChannelSet() == main
           && (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo())
           && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host Automation arrives on the audio thread, so the snapshot is taken here, once per block, and handed over
    // only when it differs from the last one.
    const auto settings = reader.read();
    if (! publishedSincePrepare || settings != publishedSettings)
    {
        publishedSettings = settings;
        publishedSincePrepare = true;
        engine.setSettings (settings);
    }

    auto main = getBusBuffer (buffer, false, 0);
    const auto sidechainBuffer = getBusBuffer (buffer, true, 1);
    const ConstAudioBlock sidechain { sidechainBuffer.getArrayOfReadPointers(), sidechainBuffer.getNumChannels(), sidechainBuffer.getNumSamples() };
    engine.process ({ main.getArrayOfWritePointers(), main.getNumChannels(), main.getNumSamples() },
                    sidechain.numChannels > 0 ? &sidechain : nullptr);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

namespace
{
const juce::Identifier versionProperty { "version" }, parameterType { "PARAM" }, idProperty { "id" }, valueProperty { "value" };

// Brings a saved state from an older version up to stateVersion, one version at a time. Version 1 is the first.
void migrate (juce::ValueTree& state)
{
    if (static_cast<int> (state.getProperty (versionProperty, 0)) < 1)
        state.setProperty (versionProperty, 1, nullptr);
}

// The plain value a saved one restores to: itself within the parameter's range, the nearer end when it is a finite
// number outside it, and the default when it isn't a finite number (nan, inf). Read in double, so a value a float
// can't hold ("1e39") is still a finite one to clamp. Text that isn't a number reads as 0, as juce::var converts it.
double restoredValue (const juce::RangedAudioParameter& parameter, const juce::var& saved)
{
    const auto& range = parameter.getNormalisableRange();
    const double value = static_cast<double> (saved);
    if (! std::isfinite (value))
        return static_cast<double> (parameter.convertFrom0to1 (parameter.getDefaultValue()));
    return juce::jlimit (static_cast<double> (range.start), static_cast<double> (range.end), value);
}
} // namespace

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (versionProperty, stateVersion, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;
    auto saved = juce::ValueTree::fromXml (*xml);
    migrate (saved);
    // The state rebuilt from what this version knows: every parameter, at its saved value restored, or at its
    // default when the session has none. Anything else a session holds (a newer version's settings) is left out.
    juce::ValueTree state (parameters.state.getType());
    for (auto* p : getParameters())
    {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (p);
        if (ranged == nullptr)
            continue;
        const auto setting = saved.getChildWithProperty (idProperty, ranged->getParameterID());
        const auto value = setting.hasType (parameterType) && setting.hasProperty (valueProperty)
                               ? restoredValue (*ranged, setting.getProperty (valueProperty))
                               : static_cast<double> (ranged->convertFrom0to1 (ranged->getDefaultValue()));
        state.appendChild (juce::ValueTree (parameterType).setProperty (idProperty, ranged->getParameterID(), nullptr).setProperty (valueProperty, value, nullptr),
                           nullptr);
    }
    parameters.replaceState (state);
}

} // namespace comp1

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new comp1::PluginProcessor();
}
