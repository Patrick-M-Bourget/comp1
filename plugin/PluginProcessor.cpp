#include "PluginProcessor.h"

#include "comp1/Latency.h"

namespace comp1
{

namespace
{
const juce::Identifier versionProperty { "version" };

// Brings a saved state, or a Preset, from an older version up to stateVersion, one version at a time.
void migrate (juce::ValueTree& state)
{
    const int version = state.getProperty (versionProperty, 0);
    if (version < 1)
        state.setProperty (versionProperty, 1, nullptr);
    // 1 to 2: A/B Compare. The settings are side A's, B is a copy of them, and neither has a Loaded Preset.
    if (version < 2)
    {
        state.appendChild (plugkit::ui::ABCompare::initialState(), nullptr);
        state.setProperty (versionProperty, 2, nullptr);
    }
}

plugkit::ui::Session::Config sessionConfig (juce::File userSettingsFile, juce::File presetFolder)
{
    plugkit::ui::Session::Config config;
    config.productName = "comp1";
    // The root type is the saved state's, comp1. The extension and the type are permanent once released: a
    // Preset file keeps both.
    config.presetFileExtension = ".comp1preset";
    config.stateVersion = PluginProcessor::stateVersion;
    config.migrate = migrate;
    config.window = PluginProcessor::window;
    config.userSettingsFile = std::move (userSettingsFile);
    config.presetFolder = std::move (presetFolder);
    return config;
}
} // namespace

PluginProcessor::PluginProcessor() : PluginProcessor (plugkit::ui::UserSettings::defaultFile ("comp1"), {}) {}

PluginProcessor::PluginProcessor (juce::File userSettingsFile, juce::File presetFolder)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "comp1", parameters::createLayout()),
      sessionState (*this, parameters, sessionConfig (std::move (userSettingsFile), std::move (presetFolder)))
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
    // only when it differs from the last one. Detection Audition, held by the editor, rides along.
    auto settings = reader.read();
    settings.detectionAudition = detectionAudition();
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

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = sessionState.saved().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;
    setDetectionAudition (false);
    sessionState.restore (juce::ValueTree::fromXml (*xml));
    reportLatency();
}

} // namespace comp1

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new comp1::PluginProcessor();
}
