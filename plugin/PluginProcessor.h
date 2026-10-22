#pragma once

#include "Parameters.h"
#include "comp1/Engine.h"
#include "comp1/Style.h"
#include "plugkit/ui/ClipLights.h"
#include "plugkit/ui/OutputLevelSource.h"
#include "plugkit/ui/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace comp1
{

// The Expanded View's display tabs: the scrolling level and Gain Reduction graph, and the Detection EQ.
enum class DisplayTab
{
    level,
    detectionEq,
};

// The Plugin Shell: the JUCE processor around the Engine. It turns the host parameters into Settings once a
// block, and saves, restores and edits them through its Session: User Presets, A/B Compare and undo and redo. Not final:
// a test's processor derives from it only to keep its per-user settings and User Presets in a folder of its own.
class PluginProcessor : public juce::AudioProcessor, public plugkit::ui::OutputLevelSource
{
public:
    // As it ships: the per-user settings in UserSettings::defaultFile ("comp1"), and User Presets in
    // Documents/comp1/Presets.
    PluginProcessor();
    // With the per-user settings file and the User Preset folder given, so a test keeps both in a folder of its own.
    PluginProcessor (juce::File userSettingsFile, juce::File presetFolder);
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
    // The host's own bypass: played as processBlock() plays, with Global Bypass forced on, so it is delayed by the
    // reported latency in each Processing Mode and crossfades in and out click-free. The Global Bypass host parameter
    // isn't written.
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlockBypassed;

    // The Expanded View (PluginEditor.h).
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // The version of the saved state's format, and of a Preset's. Restoring a session or loading a Preset brings an
    // older one up to it one version at a time, and loads what it knows of a newer one. Bump it, and add a step to
    // the migration, whenever the format changes. Version 1 is the parameters' state with its version; version 2
    // adds A/B Compare.
    static constexpr int stateVersion = 2;

    // The Expanded View's logical size (as at 100% UI Scale), and its limits.
    static constexpr plugkit::ui::Session::Window window { { 1200, 760 }, { 960, 600 }, { 2560, 1600 } };

    void getStateInformation (juce::MemoryBlock& destData) override;
    // Restoring a state of comp1's lets go of Detection Audition and puts out the Clip Lights, then restores through
    // the Session and reports the restored Processing Mode's latency. Anything else changes nothing.
    void setStateInformation (const void* data, int sizeInBytes) override;

    // The session state the editor edits through: the undo history, A/B Compare and the Preset Library. A Preset
    // load, an A/B switch or Copy, and their undo and redo may change Processing Mode, and the reported latency
    // follows it.
    plugkit::ui::Session& session() { return sessionState; }
    // The host parameters, which the editor's controls attach to.
    juce::AudioProcessorValueTreeState& parameterState() { return parameters; }

    // The Styles the editor offers: the ones with defaults to write (comp1/Style.h), which is all eight, in the frozen
    // order, the four Character Styles then the four Source Styles.
    static const std::vector<Style>& offeredStyles();
    // Chooses an offered Style, from the message thread, as the editor's Style picker does: writes the Style parameter
    // and exactly that Style's defaults (ADR 0002), its default Comp and Comp Follow on among them, never Auto Gain,
    // inside gestures, as one undo step, and returns true. Choosing the
    // Style already chosen writes its defaults again. A Style not offered changes nothing and returns false. Nothing
    // else writes a Style's defaults: restoring a session, loading a Preset, an A/B switch, undo and redo put back the
    // values they hold, and Host Automation can't set Style.
    bool chooseStyle (Style style);

    // Comp Follow from the editor (ADR 0004), from the message thread: the plugin writes parameters only inside these
    // and chooseStyle(), never on its own. Each writes inside gestures, as one undo step, and inside a transaction the
    // editor holds, as part of its step, so the switch and the drag that follows are one undo step.
    //
    // stopFollowingComp(), as the editor turns Comp Follow off by editing Threshold or Ratio: while Comp Follow is on,
    // writes it off, the Comp Curve's Threshold and Ratio at Comp, and Auto Gain on, so the output doesn't change, and
    // returns true. With Comp Follow off it changes nothing and returns false.
    bool stopFollowingComp();
    // followComp(), as the editor turns Comp Follow on by grabbing Comp: while Comp Follow is off, writes Comp at the
    // position nearest the stored Threshold on the Style's Comp Curve, clamped to its span, and Comp Follow on, and
    // returns true; the Engine then glides onto the Comp Curve. With Comp Follow on it changes nothing and returns false.
    bool followComp();
    // The Threshold in dB and the Ratio (:1) the Style's Comp Curve sets at the current Comp: what the Engine plays
    // while Comp Follow is on, and what the editor shows then. Any thread.
    double followedThresholdDb() const;
    double followedRatio() const;

    // Whether the editor offers Stereo Placement and Stereo Link: only on a stereo track. On a mono one both keep
    // their stored values and change nothing, the mono channel compressed as Stereo.
    bool isStereoPlacementAvailable() const { return getMainBusNumOutputChannels() == 2; }
    bool isStereoLinkAvailable() const { return getMainBusNumOutputChannels() == 2; }

    // The Output Meter's source, from the Engine's Output Level, from the message thread: one bar per main channel.
    // A peak above 0 dBFS lights its channel's Clip Light, which stays lit until cleared, whether or not the editor is
    // open. Restoring a session puts them out; they aren't saved.
    int outputLevelChannels() const override { return engine.outputLevelChannels(); }
    plugkit::OutputLevel readOutputLevel (int channel) override { return clipLights.note (channel, engine.readOutputLevel (channel)); }
    bool isClipLit (int channel) const override { return clipLights.isLit (channel); }
    void clearClipLights() override { clipLights.clear(); }

    // Detection Audition, while the editor holds it: not a host parameter, and never saved. Any thread.
    void setDetectionAudition (bool held) { heldDetectionAudition.store (held, std::memory_order_relaxed); }
    bool detectionAudition() const { return heldDetectionAudition.load (std::memory_order_relaxed); }

private:
    // processBlock(), with Global Bypass forced on while the host bypasses.
    void play (juce::AudioBuffer<float>& buffer, bool hostBypassed);

    juce::AudioProcessorValueTreeState parameters;
    plugkit::ui::Session sessionState; // built after the parameters it holds
    parameters::SettingsReader reader { parameters }; // read on the audio thread
    Engine engine;
    // The last Settings handed to the Engine, kept on the audio thread; the first block after prepareToPlay()
    // hands over whatever it reads.
    Settings publishedSettings;
    bool publishedSincePrepare = false;
    std::atomic<bool> heldDetectionAudition { false };
    plugkit::ui::ClipLights clipLights;

    // Reports to the host the latency of the Processing Mode the host parameter holds (comp1/Latency.h), at the
    // sample rate last prepared: at prepareToPlay(), whenever Processing Mode changes (from the editor, or a host that
    // sets it anyway), and after a state restore. Any thread may call it: setLatencySamples() is safe from any, as
    // JUCE's wrappers pass the change to the host on the message thread.
    void reportLatency();
    std::atomic<double> preparedSampleRate { 0.0 };
    struct ProcessingModeListener final : juce::AudioProcessorValueTreeState::Listener
    {
        explicit ProcessingModeListener (PluginProcessor& p) : processor (p) {}
        void parameterChanged (const juce::String&, float) override { processor.reportLatency(); }
        PluginProcessor& processor;
    } processingModeListener { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace comp1
