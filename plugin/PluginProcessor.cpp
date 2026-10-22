#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "comp1/Latency.h"
#include "plugkit/ui/SetTogether.h"

#include <utility>

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

// The host parameters choosing a Style writes, as plain values from settings: Style itself, then the settings ADR 0002
// lists (the Character and the Comp Curve are Style's). Every other parameter is Auto Gain, routing or a level, which
// a Style never touches.
std::vector<std::pair<juce::String, float>> styleWrites (Style style, const Settings& s)
{
    const auto plain = [] (double value) { return static_cast<float> (value); };
    const auto onOff = [] (bool on) { return on ? 1.0f : 0.0f; };
    return {
        { parameters::styleId, static_cast<float> (style) },
        { parameters::compId, plain (s.comp) },
        { parameters::compFollowId, onOff (s.compFollow) },
        { parameters::colourId, plain (s.colour) },
        { parameters::thresholdId, plain (s.thresholdDb) },
        { parameters::ratioId, plain (s.ratio) },
        { parameters::kneeId, plain (s.kneeDb) },
        { parameters::attackId, plain (s.attackMs) },
        { parameters::releaseId, plain (s.releaseMs) },
        { parameters::autoReleaseId, onOff (s.autoRelease) },
        { parameters::holdId, plain (s.holdMs) },
        { parameters::rangeId, plain (s.rangeDb) },
        { parameters::gateId, onOff (s.gate.on) },
        { parameters::gateThresholdId, plain (s.gate.thresholdDb) },
        { parameters::gateRangeId, plain (s.gate.rangeDb) },
        { parameters::gateAttackId, plain (s.gate.attackMs) },
        { parameters::gateReleaseId, plain (s.gate.releaseMs) },
        { parameters::gateHoldId, plain (s.gate.holdMs) },
        { parameters::limiterId, onOff (s.limiter) },
        { parameters::ceilingId, plain (s.ceilingDb) },
    };
}

// writes as one undo step inside gestures, as the editor's own edits are: one transaction around them, so the edit is
// one step however the host orders the gestures, and one step with the editor's own when it holds a transaction too.
void setAsOneEdit (juce::AudioProcessorValueTreeState& parameters,
                   plugkit::ui::EditHistory& history,
                   const std::vector<std::pair<juce::String, float>>& writes)
{
    std::vector<plugkit::ui::NewValue> values;
    for (const auto& [id, plain] : writes)
    {
        auto* parameter = parameters.getParameter (id);
        values.push_back ({ parameter, parameter->convertTo0to1 (plain) });
    }
    history.beginTransaction();
    plugkit::ui::setTogether (values);
    history.endTransaction();
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
    play (buffer, false);
}

void PluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    play (buffer, true);
}

void PluginProcessor::play (juce::AudioBuffer<float>& buffer, bool hostBypassed)
{
    // Host Automation arrives on the audio thread, so the Settings are read here, once per block, and handed over
    // only when it differs from the last one. Detection Audition, held by the editor, rides along, and so does the
    // host's bypass, as Global Bypass.
    auto settings = reader.read();
    settings.detectionAudition = detectionAudition();
    settings.globalBypass = settings.globalBypass || hostBypassed;
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

const std::vector<Style>& PluginProcessor::offeredStyles()
{
    static const std::vector<Style> offered = [] {
        std::vector<Style> withDefaults;
        for (int i = 0; i < styleCount; ++i)
            if (hasDefaults (static_cast<Style> (i)))
                withDefaults.push_back (static_cast<Style> (i));
        return withDefaults;
    }();
    return offered;
}

bool PluginProcessor::chooseStyle (Style style)
{
    auto settings = reader.read();
    if (! applyStyle (style, settings))
        return false;
    setAsOneEdit (parameters, sessionState.editHistory(), styleWrites (style, settings));
    return true;
}

double PluginProcessor::followedThresholdDb() const
{
    const auto settings = reader.read();
    return compThresholdDb (settings.compCurve, settings.comp);
}

double PluginProcessor::followedRatio() const
{
    const auto settings = reader.read();
    return compRatio (settings.compCurve, settings.comp);
}

bool PluginProcessor::stopFollowingComp()
{
    const auto settings = reader.read();
    if (! settings.compFollow)
        return false;
    // Comp Follow last, so a block the audio thread reads part-way through still plays the Comp Curve.
    setAsOneEdit (parameters,
                  sessionState.editHistory(),
                  { { parameters::thresholdId, static_cast<float> (compThresholdDb (settings.compCurve, settings.comp)) },
                    { parameters::ratioId, static_cast<float> (compRatio (settings.compCurve, settings.comp)) },
                    { parameters::autoGainId, 1.0f },
                    { parameters::compFollowId, 0.0f } });
    return true;
}

bool PluginProcessor::followComp()
{
    const auto settings = reader.read();
    if (settings.compFollow)
        return false;
    // Comp first, so Comp Follow turns on at the Comp it glides to.
    setAsOneEdit (parameters,
                  sessionState.editHistory(),
                  { { parameters::compId, static_cast<float> (compNearestThreshold (settings.compCurve, settings.thresholdDb)) },
                    { parameters::compFollowId, 1.0f } });
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
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
