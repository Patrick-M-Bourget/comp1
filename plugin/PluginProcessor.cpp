#include "PluginProcessor.h"

#include "comp1/Latency.h"

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
    parameters.addParameterListener (parameters::processingModeId, &processingModeListener);
}

PluginProcessor::~PluginProcessor()
{
    parameters.removeParameterListener (parameters::processingModeId, &processingModeListener);
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock, getMainBusNumOutputChannels());
    publishedSincePrepare = false;
    preparedSampleRate = sampleRate;
    reportLatency();
}

void PluginProcessor::reportLatency()
{
    // Before the first prepareToPlay() there is no sample rate to report a latency at; prepareToPlay() reports it.
    if (const double sampleRate = preparedSampleRate; sampleRate > 0.0)
        setLatencySamples (comp1::latencySamples (reader.read().processingMode, sampleRate));
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
    // Host Automation arrives on the audio thread, so the Settings are read here, once per block, and handed over
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

double defaultPlainValue (const juce::RangedAudioParameter& parameter)
{
    return static_cast<double> (parameter.convertFrom0to1 (parameter.getDefaultValue()));
}

// The plain value a saved one restores to: itself within the parameter's range, the nearer end when it is a finite
// number outside it, and the default when it isn't a number (nan) or is an infinity the range doesn't end at (Ratio's
// ends at ∞:1). Read in double, so a value a float can't hold ("1e39") is still a finite one to clamp. Text that
// isn't a number reads as 0, as juce::var converts it.
double restoredValue (const juce::RangedAudioParameter& parameter, const juce::var& saved)
{
    const auto& range = parameter.getNormalisableRange();
    const double value = static_cast<double> (saved);
    if (std::isinf (value) && (juce::exactlyEqual (value, static_cast<double> (range.start)) || juce::exactlyEqual (value, static_cast<double> (range.end))))
        return value;
    if (! std::isfinite (value))
        return defaultPlainValue (parameter);
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
    auto state = juce::ValueTree::fromXml (*xml);
    migrate (state);
    state.removeProperty (versionProperty, nullptr);
    // Every parameter this version knows reaches it at its saved value, restored, or at its default when the
    // session has none. Whatever else a session holds (a newer version's settings) is kept as it is, and saved
    // again, so a session saved in this version loses nothing when the newer version opens it again.
    for (auto* p : getParameters())
    {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (p);
        if (ranged == nullptr)
            continue;
        auto setting = state.getChildWithProperty (idProperty, ranged->getParameterID());
        if (! setting.hasType (parameterType))
        {
            setting = juce::ValueTree (parameterType).setProperty (idProperty, ranged->getParameterID(), nullptr);
            setting.setProperty (valueProperty, defaultPlainValue (*ranged), nullptr);
            state.appendChild (setting, nullptr);
            continue;
        }
        // A value that restores as itself is left untouched, so its text saves again byte for byte.
        const auto saved = setting.getProperty (valueProperty);
        if (const double restored = restoredValue (*ranged, saved); ! setting.hasProperty (valueProperty) || ! juce::exactlyEqual (restored, static_cast<double> (saved)))
            setting.setProperty (valueProperty, restored, nullptr);
    }
    parameters.replaceState (state);
    reportLatency();
}

} // namespace comp1

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new comp1::PluginProcessor();
}
