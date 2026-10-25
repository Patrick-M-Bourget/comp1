#pragma once

#include "Parameters.h"
#include "comp1/Engine.h"
#include "comp1/Style.h"
#include "comp1/TransferCurve.h"
#include "plugkit/ui/ClipLights.h"
#include "plugkit/ui/OutputLevelSource.h"
#include "plugkit/ui/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iterator>
#include <vector>

namespace comp1
{

// The Expanded View's two display tabs: the scrolling level and Gain Reduction graph, with the Transfer Curve inset over
// it, and the Spectrum. count isn't a tab: it is how many there are, for the tables' checks, and no list the user sees has it.
enum class DisplayTab
{
    level,
    spectrum,
    count,
};

// A display tab's row in displayTabs: the tab, and the name the session saves it under. A saved name is permanent once
// released: sessions keep it. Retired names, of tabs the display no longer has, which a session may still hold and no
// tab may take again: "detection_eq" and "transfer_curve". Restore reads them as Level.
struct DisplayTabRow
{
    DisplayTab tab;
    const char* savedName;
};

// Every display tab, a row each, in DisplayTab's order, which is the display's left to right. The display's own table
// (Display::tabs) has a row for each of these. Restore reads any other name, a session saved without one included, as
// Level.
inline constexpr DisplayTabRow displayTabs[] {
    { DisplayTab::level, "level" },
    { DisplayTab::spectrum, "spectrum" },
};

// A display tab's row in displayTabs.
constexpr std::size_t rowOf (DisplayTab tab) { return static_cast<std::size_t> (tab); }

static_assert (
    [] {
        for (std::size_t row = 0; row < std::size (displayTabs); ++row)
            if (rowOf (displayTabs[row].tab) != row)
                return false;
        return true;
    }(),
    "displayTabs lists every DisplayTab once, in DisplayTab's order");
static_assert (std::size (displayTabs) == rowOf (DisplayTab::count), "displayTabs has a row for every DisplayTab");

// The editor's views: the Compact View, and the Expanded View with the display and every section.
enum class View
{
    expanded,
    compact,
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

    // The editor, in the view the processor holds (PluginEditor.h).
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

    // The Expanded View's logical size (as at 100% UI Scale), and its limits: at the minimum every section fits, the
    // section rows' grid (both rows as wide, Detection under Gate and Compressor, Lookahead under Character)
    // inside the window's padding, and each size is whole pixels at every UI Scale.
    static constexpr plugkit::ui::Session::Window expandedWindow { { 1400, 860 }, { 1356, 720 }, { 2560, 1600 } };
    // The Compact View's, opening at its minimum: there the header's groups fit, setting the width (Undo, Redo, A/B
    // Compare and Copy on one side and the Preset group at its narrowest between, the wordmark's side as wide), the
    // body's row and the footer at its widest fit beside them, and the Preset browser's panel fits inside its margin
    // at a smaller size; the height is the row's with a tall Gain Reduction meter. Each size is whole pixels at every
    // UI Scale.
    static constexpr plugkit::ui::Session::Window compactWindow { { 780, 420 }, { 780, 420 }, { 1600, 1000 } };
    static const plugkit::ui::Session::Window& windowOf (View view) { return view == View::compact ? compactWindow : expandedWindow; }
    // The Session's window, the envelope of both views': the Expanded View's default, the smaller minimum and the
    // larger maximum on each axis. The editor holds the window to the shown view's own limits.
    static constexpr plugkit::ui::Session::Window sessionWindow {
        expandedWindow.defaultSize,
        { std::min (expandedWindow.minimum.x, compactWindow.minimum.x), std::min (expandedWindow.minimum.y, compactWindow.minimum.y) },
        { std::max (expandedWindow.maximum.x, compactWindow.maximum.x), std::max (expandedWindow.maximum.y, compactWindow.maximum.y) }
    };

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
    // Chooses an offered Style, from the message thread, as the footer's Style chip does: writes the Style parameter
    // and exactly that Style's defaults (ADR 0002), its default Comp, Comp Follow on and Auto Gain on among
    // them, inside gestures, as one undo step, and returns true. Choosing the
    // Style already chosen writes its defaults again. A Style not offered changes nothing and returns false. Nothing
    // else writes a Style's defaults: restoring a session, loading a Preset, an A/B switch, undo and redo put back the
    // values they hold, and Host Automation can't set Style.
    bool chooseStyle (Style style);

    // Comp Follow from the editor (ADR 0004), from the message thread: the plugin writes parameters only inside these
    // and chooseStyle(), never on its own. Each writes inside gestures, as one undo step, and inside a transaction the
    // editor holds, as part of its step, so the switch and the drag that follows are one undo step.
    //
    // stopFollowingComp(), as the editor turns Comp Follow off by editing Threshold or Ratio: while Comp Follow is on,
    // writes it off and the Comp Curve's Threshold and Ratio at Comp, so the output doesn't change, and
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
    // The Transfer Curve's settings the Engine plays at the host parameters: the Comp Curve's Threshold and Ratio at
    // Comp while Comp Follow is on (followedThresholdDb, followedRatio), else the stored ones; Knee and Range stored.
    // Any thread.
    clean::Curve playedCurve() const;
    // The Settings the host parameters hold, as the Plugin Shell hands them to the Engine. Any thread.
    Settings heldSettings() const;
    // Auto Gain's estimate in dB (comp1/AutoGain.h, ADR 0010) for the settings the host parameters hold, from the
    // played Transfer Curve, the timing and the Character, while Auto Gain is on, with Comp Follow on or off; else 0.
    // What the Engine glides its Auto Gain to. Any thread.
    double appliedAutoGainDb() const;

    // Whether the editor offers Stereo Placement and Stereo Link: only on a stereo track. On a mono one both keep
    // their stored values and change nothing, the mono channel compressed as Stereo.
    bool isStereoPlacementAvailable() const { return getMainBusNumOutputChannels() == 2; }
    bool isStereoLinkAvailable() const { return getMainBusNumOutputChannels() == 2; }

    // The sample rate prepareToPlay() was last given, in Hz, or 0 before the first: the rate the Engine plays at, so the
    // one the editor offers Oversampling at (comp1/Latency.h's maxOversampling). Any thread.
    double preparedSampleRateHz() const { return preparedSampleRate.load (std::memory_order_relaxed); }

    // The Output Meter's source, from the Engine's Output Level, from the message thread: one bar per main channel.
    // A peak above 0 dBFS lights its channel's Clip Light, which stays lit until cleared, whether or not the editor is
    // open. Restoring a session puts them out; they aren't saved.
    int outputLevelChannels() const override { return engine.outputLevelChannels(); }
    plugkit::OutputLevel readOutputLevel (int channel) override { return clipLights.note (channel, engine.readOutputLevel (channel)); }
    bool isClipLit (int channel) const override { return clipLights.isLit (channel); }
    void clearClipLights() override { clipLights.clear(); }

    // The Engine's level history, for the editor's one reader (LevelHistory.h), from the message thread: a record per
    // 16-sample run played since the last read, oldest first (Engine::readLevels, skipLevels, levelHistoryCapacity).
    int readLevels (LevelRecord* destination, int maxRecords) { return engine.readLevels (destination, maxRecords); }
    void skipLevels() { engine.skipLevels(); }
    int levelHistoryCapacity() const { return engine.levelHistoryCapacity(); }

    // The Engine's input tap and output tap, for the Spectrum display, their one reader, from the message thread: the
    // main input before any stage, and the Wet path directly after the compressor, each a mono sample per sample played
    // since the last read, oldest first (Engine::read, skip and capacity).
    int read (Tap tap, float* destination, int maxSamples) { return engine.read (tap, destination, maxSamples); }
    void skip (Tap tap) { engine.skip (tap); }
    int capacity (Tap tap) const { return engine.capacity (tap); }

    // The display tab the Expanded View shows: saved with the session, read back on restore (Level for a session saved
    // without it, or with a tab there no longer is), and in no Preset; not an undo step, and it makes no side Modified. Any thread.
    DisplayTab displayTab() const { return chosenDisplayTab.load (std::memory_order_relaxed); }
    void setDisplayTab (DisplayTab tab) { chosenDisplayTab.store (tab, std::memory_order_relaxed); }

    // The view the editor shows: Expanded for a new instance; saved with the session, read back on restore (Expanded
    // for a session saved without it), and in no Preset; not an undo step, and it makes no side Modified. Each view
    // keeps its own window size: the shown view's is the Session's (Session::editorSize), the hidden view's is
    // hiddenViewSize(). Showing the other view swaps them, so it opens at its last size, or its default the first
    // time; showing the view shown changes nothing. Any thread.
    View view() const { return chosenView.load (std::memory_order_relaxed); }
    void setView (View view);
    // The hidden view's logical size (as at 100% UI Scale), within its limits: its default until it has been shown.
    juce::Point<int> hiddenViewSize() const;

    // Whether the Expanded View's Detection section is open: collapsed for a new instance; saved with the session,
    // read back on restore (collapsed for a session saved without it), and in no Preset; not an undo step, and it makes
    // no side Modified. Any thread.
    bool detectionOpen() const { return openDetection.load (std::memory_order_relaxed); }
    void setDetectionOpen (bool open) { openDetection.store (open, std::memory_order_relaxed); }

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
    std::atomic<DisplayTab> chosenDisplayTab { DisplayTab::level };
    std::atomic<View> chosenView { View::expanded };
    // The hidden view's size in logical pixels, 0 by 0 until it has one.
    std::atomic<int> hiddenWidth { 0 }, hiddenHeight { 0 };
    std::atomic<bool> openDetection { false };

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
