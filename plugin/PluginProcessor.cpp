#include "PluginProcessor.h"

#include "FactoryPresets.h"
#include "PluginEditor.h"

#include "comp1/AutoGain.h"
#include "comp1/Latency.h"
#include "plugkit/ui/SetTogether.h"

#include <utility>

namespace comp1
{

namespace
{
const juce::Identifier versionProperty { "version" };
// The display tab, a view property the session saves and restore takes off. Its name and values are permanent once
// released: sessions keep them.
const juce::Identifier displayTabProperty { "display_tab" };
const juce::String levelTab { "level" }, transferCurveTab { "transfer_curve" };

// A display tab as the session saves it, and back: anything else, a session saved without one included, is Level. So
// is "detection_eq", a tab older sessions hold that the display no longer has.
const juce::String& savedTab (DisplayTab tab)
{
    switch (tab)
    {
        case DisplayTab::level: return levelTab;
        case DisplayTab::transferCurve: return transferCurveTab;
    }
    return levelTab;
}

DisplayTab tabSaved (const juce::String& saved)
{
    if (saved == transferCurveTab)
        return DisplayTab::transferCurve;
    return DisplayTab::level;
}

// The view, and the hidden view's size in logical pixels: view properties the session saves and restore takes off.
// Their names and values are permanent once released: sessions keep them.
const juce::Identifier viewProperty { "view" }, hiddenViewWidthProperty { "hidden_view_width" }, hiddenViewHeightProperty { "hidden_view_height" };
const juce::String expandedView { "expanded" }, compactView { "compact" };

// Whether the Detection section is open: a view property the session saves and restore takes off. Its name and values
// are permanent once released: sessions keep them.
const juce::Identifier detectionOpenProperty { "detection_open" };
const juce::String openDetectionSection { "open" }, collapsedDetectionSection { "collapsed" };

View otherThan (View view) { return view == View::compact ? View::expanded : View::compact; }

// A logical size held to window's limits.
juce::Point<int> within (const plugkit::ui::Session::Window& window, juce::Point<int> size)
{
    return { juce::jlimit (window.minimum.x, window.maximum.x, size.x), juce::jlimit (window.minimum.y, window.maximum.y, size.y) };
}

// A saved size's side: a finite number held to its limits and rounded, or none (0).
int restoredSide (const juce::var& saved, int minimum, int maximum)
{
    const auto text = saved.toString().trim();
    const double side = text.getDoubleValue();
    if (text.isEmpty() || ! std::isfinite (side) || ! text.containsAnyOf ("0123456789"))
        return 0;
    return juce::jlimit (minimum, maximum, juce::roundToInt (juce::jlimit (static_cast<double> (minimum), static_cast<double> (maximum), side)));
}

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

// The Factory Presets the plugin bundles (presets/factory), each named by its file's name without the extension. The
// names are UTF-8, as they hold an en dash ("Vocals \u2013 One-Knob Lead").
std::vector<plugkit::ui::PresetLibrary::FactoryPresetData> factoryPresets (const juce::String& fileExtension)
{
    std::vector<plugkit::ui::PresetLibrary::FactoryPresetData> presets;
    for (int i = 0; i < FactoryPresets::namedResourceListSize; ++i)
    {
        int size = 0;
        const auto* data = FactoryPresets::getNamedResource (FactoryPresets::namedResourceList[i], size);
        presets.push_back ({ juce::String::fromUTF8 (FactoryPresets::originalFilenames[i]).upToLastOccurrenceOf (fileExtension, false, false),
                             data,
                             size });
    }
    return presets;
}

plugkit::ui::Session::Config sessionConfig (juce::File userSettingsFile, juce::File presetFolder)
{
    plugkit::ui::Session::Config config;
    config.productName = "comp1";
    // The root type is the saved state's, comp1. The extension and the type are permanent once released: a
    // Preset file keeps both.
    config.presetFileExtension = ".comp1preset";
    config.factoryPresets = factoryPresets (config.presetFileExtension);
    config.stateVersion = PluginProcessor::stateVersion;
    config.migrate = migrate;
    config.window = PluginProcessor::sessionWindow;
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

clean::Curve PluginProcessor::playedCurve() const
{
    const auto settings = reader.read();
    auto curve = clean::curveOf (settings);
    if (settings.compFollow)
    {
        curve.thresholdDb = compThresholdDb (settings.compCurve, settings.comp);
        curve.inverseRatio = compInverseRatio (settings.compCurve, settings.comp);
    }
    return curve;
}

double PluginProcessor::appliedAutoGainDb() const
{
    const auto settings = reader.read();
    if (! settings.autoGain && ! settings.compFollow)
        return 0.0;
    return autogain::estimateDb (playedCurve(),
                                 { .attackMs = settings.attackMs,
                                   .releaseMs = settings.releaseMs,
                                   .holdMs = settings.holdMs,
                                   .autoRelease = settings.autoRelease,
                                   .character = settings.character });
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

void PluginProcessor::setView (View shown)
{
    if (shown == view())
        return;
    const auto next = hiddenViewSize();
    const auto last = within (windowOf (view()), sessionState.editorSize());
    hiddenWidth = last.x;
    hiddenHeight = last.y;
    sessionState.setEditorSize (next);
    chosenView = shown;
}

juce::Point<int> PluginProcessor::hiddenViewSize() const
{
    const auto& hidden = windowOf (otherThan (view()));
    const juce::Point<int> stored { hiddenWidth.load(), hiddenHeight.load() };
    return stored.x > 0 && stored.y > 0 ? within (hidden, stored) : hidden.defaultSize;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = sessionState.saved();
    state.setProperty (displayTabProperty, savedTab (displayTab()), nullptr);
    state.setProperty (viewProperty, view() == View::compact ? compactView : expandedView, nullptr);
    const auto hidden = hiddenViewSize();
    state.setProperty (hiddenViewWidthProperty, hidden.x, nullptr);
    state.setProperty (hiddenViewHeightProperty, hidden.y, nullptr);
    state.setProperty (detectionOpenProperty, detectionOpen() ? openDetectionSection : collapsedDetectionSection, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;
    setDetectionAudition (false);
    clearClipLights();
    sessionState.restore (juce::ValueTree::fromXml (*xml), [this] (juce::ValueTree& state) {
        setDisplayTab (tabSaved (state.getProperty (displayTabProperty).toString()));
        state.removeProperty (displayTabProperty, nullptr);
        // The Session holds the restored size to the envelope of both views; the shown view's holds it to its own.
        const auto shown = state.getProperty (viewProperty).toString() == compactView ? View::compact : View::expanded;
        chosenView = shown;
        sessionState.setEditorSize (within (windowOf (shown), sessionState.editorSize()));
        const auto& hidden = windowOf (otherThan (shown));
        hiddenWidth = restoredSide (state.getProperty (hiddenViewWidthProperty), hidden.minimum.x, hidden.maximum.x);
        hiddenHeight = restoredSide (state.getProperty (hiddenViewHeightProperty), hidden.minimum.y, hidden.maximum.y);
        // Anything but open, a session saved without it included, is collapsed.
        setDetectionOpen (state.getProperty (detectionOpenProperty).toString() == openDetectionSection);
        for (const auto& property : { viewProperty, hiddenViewWidthProperty, hiddenViewHeightProperty, detectionOpenProperty })
            state.removeProperty (property, nullptr);
    });
    reportLatency();
}

} // namespace comp1

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new comp1::PluginProcessor();
}
